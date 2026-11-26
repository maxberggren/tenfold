#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#     "demucs==4.1.0",
#     "einops==0.8.2",
#     "huggingface-hub==2.4.0",
#     "julius==0.2.8",
#     "numpy==2.5.5; sys_platform != 'darwin' or platform_machine != 'x86_64'",
#     "numpy==1.26.4; sys_platform == 'darwin' and platform_machine == 'x86_64'",
#     "pyyaml==6.0.4",
#     "safetensors==0.8.0",
#     "soundfile==0.14.0",
#     "soxr==1.1.0",
#     "torch==2.15.0; sys_platform != 'darwin' or platform_machine != 'x86_64'",
#     "torch==2.2.2; sys_platform == 'darwin' and platform_machine == 'x86_64'",
#     "tqdm==4.70.1",
# ]
#
# [tool.uv]
# # Demucs declares two dependencies that it only needs for its own file reading and MP3 writing.
# # This script reads and writes audio itself, and one of them (sphn) has no wheel for Linux on
# # ARM, where it would have to be compiled with a Rust toolchain, so both are left out. An
# # override with a condition that is never true is the way to say that to every version of uv.
# override-dependencies = ["sphn; sys_platform == 'never'", "lameenc; sys_platform == 'never'"]
# # Only the packages named above are pinned. This date pins everything they depend on as well,
# # so that the environment built next year is the one that was tested.
# exclude-newer = "2026-11-25T00:00:00Z"
#
# [tool.uv.sources]
# # On Linux the torch wheel on PyPI depends on several gigabytes of NVIDIA CUDA libraries, on
# # x86_64 and on ARM alike. Most machines have no NVIDIA card, so the default here is the
# # CPU-only build (about 200 MB). A machine that does have one switches itself to the CUDA
# # build at run time; see use_nvidia_build_if_present() below. macOS takes the PyPI wheel,
# # which is small and already contains Metal (MPS) support.
# torch = [{ index = "pytorch-cpu", marker = "sys_platform == 'linux'" }]
#
# [[tool.uv.index]]
# name = "pytorch-cpu"
# url = "https://download.pytorch.org/whl/cpu"
# explicit = true
# ///
"""Split a song into drums, bass, vocals and other, for the tenfold sampler page.

    uv run --script stems.py --in MIX.wav --out DIR [--model NAME] [--threads N]

MIX.wav is read with libsndfile; tenfold sends 48 kHz 16-bit stereo. DIR receives drums.wav,
bass.wav, vocals.wav and other.wav as 16-bit stereo PCM at the sample rate of the input, each
exactly as many frames long as the input and sample-aligned with it. The files are written
under temporary names and get their real names only when all four are complete.

Standard output carries nothing but the protocol: "STAGE <a few words>" when a phase begins,
and "PROGRESS <0..1>" twice a second while separating. On failure the exit status is not zero
and the last line on standard error is "ERROR: <one sentence>". SIGTERM stops the script at
once and leaves no partial files behind. The exit status is 0 only when all four files exist.

Which model "--model auto" picks, and why
-----------------------------------------
Measured on an Apple M2 Pro running Linux (8 fast cores, 4 slow ones, no GPU) with a song of
4 minutes 2 seconds. Quality is the average SDR over the four stems on the MVSep multisong
set, as listed by the Music-Source-Separation-Training project; higher is better.

    htdemucs       9.16 dB    35 s   one Hybrid Transformer Demucs v4 network
    htdemucs_ft    9.33 dB   139 s   four fine-tuned networks, one per stem: four times the work
    htdemucs_6s    (lower)    31 s   six stems; guitar and piano are added back into "other"

A player who clicks STEMS should wait about a minute at most. Everything that separates better
than htdemucs misses that on a CPU by a wide margin: the same song takes about five minutes
with SCNet Large, nine with BS-RoFormer and a quarter of an hour or more with SCNet XL or
SubRoformer. So:

    no GPU, or Apple Silicon (Metal)   ->  htdemucs
    NVIDIA GPU (CUDA)                  ->  htdemucs_ft, where the extra work costs seconds

Neither GPU path could be run on the development machine. If anything goes wrong on a GPU (an
operation it lacks, too little video memory) the script starts over on the CPU.

Weights are downloaded on first use from the Hugging Face hub, without an account, into the
usual Hugging Face cache (~/.cache/huggingface/hub, or $HF_HOME/hub): 84 MB for htdemucs. Each
repository is pinned to a commit.

Two environment variables exist for testing and for unusual machines. TENFOLD_STEMS_DEVICE
(cpu, cuda or mps) overrides the choice of device. TENFOLD_STEMS_NVIDIA=0 keeps the script from
fetching the CUDA build of PyTorch on a machine with an NVIDIA card; =1 makes it try even if
no NVIDIA driver is visible.
"""

import argparse
import os
import signal
import subprocess
import sys
import threading
import time
import traceback

MODEL_RATE = 44100
STEMS = ("drums", "bass", "vocals", "other")

# Each model is a Hugging Face repository at a pinned commit, so that the weights cannot change
# under a released tenfold. The file named "bag" lists the networks the model consists of.
MODELS = {
    "htdemucs": {
        "repo": "adefossez/HTDemucs",
        "revision": "cbc8a9b1a87023b7fd74e7b3412e6321c0eab003",
        "bag": "htdemucs.yaml",
        "megabytes": 84,
    },
    "htdemucs_ft": {
        "repo": "adefossez/HTDemucs-ft",
        "revision": "d74ac89c3a1e874fc78f152555cf4d8533f06cd4",
        "bag": "htdemucs_ft.yaml",
        "megabytes": 336,
    },
    "htdemucs_6s": {
        "repo": "adefossez/HTDemucs-6s",
        "revision": "3c5ee475be622df764938de97e4281a7b07ffa58",
        "bag": "htdemucs_6s.yaml",
        "megabytes": 55,
    },
}
AUTO_MODEL = {"cpu": "htdemucs", "mps": "htdemucs", "cuda": "htdemucs_ft"}

# Every Hybrid Transformer Demucs model works on segments of this length, the one it was trained
# with. The models say so themselves once loaded; the threads have to be planned before that.
SEGMENT_SECONDS = 7.8

# Neighbouring segments overlap by this fraction and are cross-faded. 0.25 is Demucs's own
# default. plan_cpu_work() may go a little lower, but never below the minimum.
OVERLAP = 0.25
MINIMUM_OVERLAP = 0.15


class Fail(Exception):
    """An error whose message is fit to show to a musician."""


OUT_OF_MEMORY = "The computer ran out of memory while separating the song."

# Exit status of the child that tries the CUDA build of PyTorch when that build works but finds
# no usable card. It must differ from the statuses uv itself fails with (1 and 2).
NO_CUDA = 77


# ---------------------------------------------------------------------------------------------
# The protocol on standard output, and stopping cleanly.

# Libraries occasionally print to standard output. The player parses that stream, so the real
# one is kept aside for the protocol and file descriptor 1 is pointed at standard error instead.
_protocol = os.fdopen(os.dup(1), "w", buffering=1, encoding="utf-8")
os.dup2(2, 1)
_protocol_lock = threading.Lock()


def say(line):
    with _protocol_lock:
        _protocol.write(line + "\n")
        _protocol.flush()


def stage(words):
    say("STAGE " + words)


_temporary_files = []
_child = None


def remove_temporary_files():
    for path in _temporary_files:
        try:
            os.unlink(path)
        except OSError:
            pass


def die(sentence, status=1):
    """Ends the process now. The sentence must be the last thing on standard error, so nothing
    else is allowed to run afterwards: no interpreter shutdown, no worker thread winding down."""
    remove_temporary_files()
    if _child is not None and _child.poll() is None:
        _child.terminate()
    try:
        sys.stderr.flush()
        os.write(2, ("ERROR: " + sentence + "\n").encode())
    finally:
        os._exit(status)


# An ordinary Python signal handler only runs when the main thread gets back to the interpreter,
# and the main thread spends whole seconds inside single calls into the resampler and PyTorch.
# So the stop signals are blocked everywhere and one thread does nothing but wait for them; it
# can end the process whatever the others are doing.
STOP_SIGNALS = (signal.SIGTERM, signal.SIGINT, signal.SIGHUP)
signal.pthread_sigmask(signal.SIG_BLOCK, STOP_SIGNALS)
# Held while the finished files are renamed into place, and never given back: once the first
# stem has its final name the other three must follow, so a cancellation then comes too late.
_committing = threading.Lock()


def wait_for_stop_signal():
    number = signal.sigwait(STOP_SIGNALS)
    with _committing:
        die("Stem separation was cancelled.", 128 + number)


threading.Thread(target=wait_for_stop_signal, daemon=True).start()


class Progress:
    """Turns "segment started" and "segment finished" events into PROGRESS lines.

    Several segments are in flight at once on a CPU and each takes a few seconds, so finished
    segments alone would leave the player without news for longer than a second. A ticker
    thread therefore reports twice a second and credits each segment in flight with the share
    of its expected duration that has passed. The reported value never goes backwards.
    """

    def __init__(self, total):
        self.total = max(1, total)
        self.done = 0
        self.spent = 0.0
        self.started = {}
        self.last = 0.0
        self.lock = threading.Lock()
        self.finished = threading.Event()
        self.ticker = threading.Thread(target=self.tick, daemon=True)

    def __enter__(self):
        say("PROGRESS 0.000")
        self.ticker.start()
        return self

    def __exit__(self, kind, value, trace):
        self.finished.set()
        self.ticker.join()
        if kind is None:
            say("PROGRESS 1.000")

    def start(self, key):
        with self.lock:
            self.started[key] = time.monotonic()

    def end(self, key):
        with self.lock:
            began = self.started.pop(key, None)
            if began is not None:
                self.spent += time.monotonic() - began
            self.done += 1

    def tick(self):
        while not self.finished.wait(0.5):
            now = time.monotonic()
            with self.lock:
                # Before the first segment has finished there is nothing to go by, so a segment
                # is assumed to take ten seconds; the estimate corrects itself after that.
                expected = self.spent / self.done if self.done else 10.0
                partial = sum(min(0.9, (now - began) / expected) for began in self.started.values())
                value = min(0.999, (self.done + partial) / self.total)
                self.last = max(self.last, value)
                value = self.last
            say("PROGRESS %.3f" % value)


# ---------------------------------------------------------------------------------------------
# Choosing processors.


def fast_cores():
    """Returns the CPUs worth computing on, or None where the system cannot say.

    Chips that mix fast and slow cores (Apple Silicon, recent Intel) lose time when a parallel
    loop has to wait for its slowest member, and two threads on one hyper-threaded core get in
    each other's way. Measured here: PyTorch left to itself on all 12 cores needed 70 s for the
    test song, and 49 s when given 8 threads and kept on the 8 fast cores.
    """
    if not sys.platform.startswith("linux"):
        return None
    allowed = sorted(os.sched_getaffinity(0))
    root = "/sys/devices/system/cpu/cpu%d/"

    def read(cpu, name):
        try:
            with open(root % cpu + name) as file:
                return file.read().strip()
        except OSError:
            return None

    # ARM reports a relative capacity per core; Intel hybrids list their fast cores in a file.
    capacity = {cpu: int(read(cpu, "cpu_capacity") or 0) for cpu in allowed}
    chosen = [cpu for cpu in allowed if capacity[cpu] == max(capacity.values())]
    try:
        with open("/sys/devices/cpu_core/cpus") as file:
            performance = set()
            for part in file.read().strip().split(","):
                first, _, last = part.partition("-")
                performance.update(range(int(first), int(last or first) + 1))
        chosen = [cpu for cpu in chosen if cpu in performance] or chosen
    except (OSError, ValueError):
        pass
    # Keep one CPU of every group of hyper-threads that share a physical core.
    seen, single = set(), []
    for cpu in chosen:
        siblings = read(cpu, "topology/thread_siblings_list") or str(cpu)
        if siblings not in seen:
            seen.add(siblings)
            single.append(cpu)
    return single or None


def fast_core_count_on_mac():
    """Asks macOS for the number of performance cores, or failing that of physical cores."""
    for name in ("hw.perflevel0.physicalcpu", "hw.physicalcpu"):
        try:
            answer = subprocess.run(["sysctl", "-n", name], capture_output=True, text=True, timeout=5)
            return int(answer.stdout)
        except (OSError, ValueError, subprocess.SubprocessError):
            continue
    return None


def choose_threads(requested):
    """Decides how many threads to compute with, and keeps them on the fast cores on Linux."""
    cores = fast_cores()
    if requested:
        if cores and requested <= len(cores):
            os.sched_setaffinity(0, cores)
        return requested
    if cores:
        os.sched_setaffinity(0, cores)
        return len(cores)
    if sys.platform == "darwin":
        return fast_core_count_on_mac() or os.cpu_count() or 4
    return os.cpu_count() or 4


def memory_gigabytes():
    """Returns the size of the machine's memory, or a cautious guess where it cannot be read."""
    try:
        return os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE") / 2**30
    except (ValueError, OSError, AttributeError):
        return 8.0


def plan_cpu_work(frames, threads):
    """Returns (segments computed at once, threads inside each, overlap between segments).

    PyTorch's own threading scales poorly for this model. The test song of 4:02 took 49 s with
    one segment at a time on 8 threads, 41 s with 8 segments at a time on one thread each, and
    35 s once the rounds were evened out as described below. Each segment in flight needs about
    half a gigabyte, so small machines get fewer workers with more threads each rather than
    being pushed into swap, and a clip of only a few segments gives each of them more threads
    rather than leaving cores idle. No gain was seen beyond 8 workers.

    The workers take segments in rounds, and a last round with only a few segments leaves most
    cores idle for its whole duration: 42 segments on 8 workers are five full rounds and a
    sixth with two. Stretching the stride a little turns that into 40 segments in five rounds,
    a sixth less waiting for an overlap of 0.226 instead of 0.25. The overlap is never taken
    below MINIMUM_OVERLAP for this; if that would be needed the ragged last round stays.
    """
    segment = int(MODEL_RATE * SEGMENT_SECONDS)

    def count(overlap):
        return len(range(0, frames, int((1 - overlap) * segment)))

    segments = count(OVERLAP)
    # Measured: 2.1 GB with one segment in flight, 5.4 GB with eight. Half the memory is ours.
    affordable = int((memory_gigabytes() / 2 - 1.6) / 0.5)
    workers = max(1, min(threads, segments, 8, affordable))
    overlap = OVERLAP
    even = segments // workers * workers
    if workers > 1 and even != segments:
        stride = -(-frames // even)
        # Half a sample of margin, so that rounding down inside Demucs lands on this stride.
        stretched = 1 - (stride + 0.5) / segment
        if stretched >= MINIMUM_OVERLAP and count(stretched) == even:
            overlap = stretched
    return workers, max(1, threads // workers), overlap


# ---------------------------------------------------------------------------------------------
# NVIDIA.


def use_nvidia_build_if_present(arguments):
    """Restarts the script with the CUDA build of PyTorch on a Linux machine with an NVIDIA card.

    The inline metadata above asks for the CPU-only build so that nobody downloads gigabytes
    of CUDA libraries for a card they do not have. "uv run --no-sources" ignores that one line
    and takes the PyPI wheel instead, which on Linux is the CUDA build; uv keeps the two
    environments side by side in its cache. The CUDA environment is first built and tried in a
    child process, so that a failed download or a driver too old for it leaves this process free
    to carry on with the CPU. A driver that the CUDA build cannot use is remembered, otherwise
    every separation would start with the same futile attempt; a failed download is not, so that
    it is tried again next time.

    The environment variable TENFOLD_STEMS_NVIDIA overrides the detection: 0 never tries, 1 tries
    even if no driver is visible. The values "probe" and "run" are set by this function itself.
    """
    wish = os.environ.get("TENFOLD_STEMS_NVIDIA", "")
    if not sys.platform.startswith("linux") or wish in ("0", "probe", "run"):
        return
    try:
        with open("/proc/driver/nvidia/version") as file:
            driver = file.read()
    except OSError:
        if wish != "1":
            return
        driver = "unknown"
    import hashlib
    import shutil

    # uv tells the programs it runs where it lives.
    uv = os.environ.get("UV") or shutil.which("uv")
    if not uv:
        return
    cache = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.expanduser("~/.cache"), "tenfold")
    marker = os.path.join(cache, "stems-cuda-unusable-" + hashlib.sha256(driver.encode()).hexdigest()[:16])
    if os.path.exists(marker):
        return
    stage("preparing the graphics card")
    command = [uv, "run", "--no-sources", "--script", os.path.abspath(__file__)]
    global _child
    try:
        # A child inherits the blocked signals, and uv has to be able to hear a cancellation.
        _child = subprocess.Popen(command, env=dict(os.environ, TENFOLD_STEMS_NVIDIA="probe"),
                                  stdout=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                                  preexec_fn=lambda: signal.pthread_sigmask(signal.SIG_UNBLOCK, STOP_SIGNALS))
        verdict = _child.wait()
    except OSError:
        verdict = None
    _child = None
    if verdict == 0:
        # From here on the CUDA environment is this script: same process, same arguments.
        os.environ["TENFOLD_STEMS_NVIDIA"] = "run"
        os.dup2(_protocol.fileno(), 1)
        signal.pthread_sigmask(signal.SIG_UNBLOCK, STOP_SIGNALS)
        os.execv(uv, command + arguments)
    if verdict == NO_CUDA:
        try:
            os.makedirs(cache, exist_ok=True)
            open(marker, "w").close()
        except OSError:
            pass


# ---------------------------------------------------------------------------------------------
# Audio in and out.


def read_song(path):
    """Returns the song as (frames, 2) floats and its sample rate. Mono is doubled, and of more
    than two channels only the first two are used."""
    import numpy as np
    import soundfile

    try:
        samples, rate = soundfile.read(path, dtype="float32", always_2d=True)
    except Exception as error:
        raise Fail("The song file could not be read as audio.") from error
    if len(samples) == 0:
        raise Fail("The song file is empty.")
    if samples.shape[1] == 1:
        samples = np.repeat(samples, 2, axis=1)
    return np.ascontiguousarray(samples[:, :2]), rate


def resample(samples, rate_in, rate_out):
    """Changes the sample rate of (frames, channels) audio with the SoX resampler at its best
    setting. Its one-shot mode compensates for the filter's delay, so the result stays aligned."""
    import soxr

    if rate_in == rate_out:
        return samples
    return soxr.resample(samples, rate_in, rate_out, quality="VHQ")


def write_stems(stems, rate, frames, folder):
    """Brings the stems back to the song's sample rate and exact length, writes the four files
    under temporary names, and only then gives all four their real names."""
    import numpy as np
    import soundfile

    pending = []
    try:
        for name in STEMS:
            # One stem at a time, so that no single call into the resampler lasts long.
            samples = resample(stems[name], MODEL_RATE, rate)
            # The round trip through 44.1 kHz can come back a frame short or long.
            if len(samples) < frames:
                samples = np.concatenate([samples, np.zeros((frames - len(samples), 2), samples.dtype)])
            # libsndfile wraps around on overflow when it converts floats, and a stem may well
            # be louder than the mix at some instant, so the conversion to 16 bits is done here.
            pcm = np.clip(np.rint(samples[:frames] * 32768.0), -32768, 32767).astype(np.int16)
            final = os.path.join(folder, name + ".wav")
            temporary = os.path.join(folder, ".%s.%d.part" % (name, os.getpid()))
            _temporary_files.append(temporary)
            soundfile.write(temporary, pcm, rate, subtype="PCM_16", format="WAV")
            pending.append((temporary, final))
        _committing.acquire()
        for temporary, final in pending:
            os.replace(temporary, final)
    except Exception as error:
        raise Fail("The stems could not be written to %s." % folder) from error
    _temporary_files.clear()


# ---------------------------------------------------------------------------------------------
# The model.


def load_model(name):
    """Fetches the weights if they are not in the cache yet and builds the model on the CPU."""
    from huggingface_hub import hf_hub_download
    import yaml
    from demucs.apply import BagOfModels
    from demucs.hf import load_safetensors_model

    spec = MODELS[name]

    def fetch(filename, cached_only):
        return hf_hub_download(spec["repo"], filename, revision=spec["revision"],
                               local_files_only=cached_only)

    def fetch_all(cached_only):
        with open(fetch(spec["bag"], cached_only)) as file:
            bag = yaml.safe_load(file)
        return bag, [fetch(signature + ".safetensors", cached_only) for signature in bag["models"]]

    try:
        bag, files = fetch_all(cached_only=True)
    except Exception:
        stage("downloading model")
        try:
            bag, files = fetch_all(cached_only=False)
        except Exception as error:
            raise Fail("The stem model (%d MB) could not be downloaded; check the internet connection "
                       "and try again." % spec["megabytes"]) from error
    stage("loading model")
    networks = [load_safetensors_model(file) for file in files]
    model = BagOfModels(networks, bag.get("weights"), bag.get("segment"))
    model.eval()
    return model


def separate(model, mix, device, workers, overlap):
    """Runs the model over (channels, frames) audio at 44.1 kHz; returns {stem: (frames, channels)}."""
    import torch
    from demucs.apply import apply_model

    wave = torch.from_numpy(mix)
    # Demucs was trained on audio normalised like this, and expects the same when it is used.
    reference = wave.mean(0)
    mean, deviation = reference.mean(), reference.std() + 1e-8
    wave = (wave - mean) / deviation

    if device != "cpu":
        # A graphics card does its own parallel work; segments go to it one after another.
        workers, overlap = 0, OVERLAP
    stride = int((1 - overlap) * int(model.samplerate * model.models[0].segment))
    total = len(range(0, wave.shape[1], stride)) * len(model.models)

    with Progress(total) as progress:
        def on_segment(event):
            key = (event["model_idx_in_bag"], event["segment_offset"])
            if event["state"] == "start":
                progress.start(key)
            else:
                progress.end(key)

        with torch.inference_mode():
            out = apply_model(model, wave[None], shifts=0, split=True, overlap=overlap, device=device,
                              num_workers=workers, callback=on_segment)[0]
    out = (out.float().cpu() * deviation + mean).numpy()

    stems = {}
    for index, source in enumerate(model.sources):
        target = source if source in STEMS else "other"
        stems[target] = stems[target] + out[index].T if target in stems else out[index].T
    return stems


def run(arguments):
    if os.environ.get("TENFOLD_STEMS_NVIDIA") == "probe":
        # This is the child started by use_nvidia_build_if_present(): it only has to say
        # whether the CUDA build that uv just installed can see the card.
        import torch

        os._exit(0 if torch.cuda.is_available() else NO_CUDA)

    parser = argparse.ArgumentParser(description="Split a song into drums, bass, vocals and other.")
    parser.add_argument("--in", dest="mix", required=True, metavar="MIX.wav")
    parser.add_argument("--out", required=True, metavar="DIR")
    parser.add_argument("--model", default="auto", choices=["auto"] + sorted(MODELS))
    parser.add_argument("--threads", type=int, default=0, metavar="N")
    options = parser.parse_args(arguments)
    if options.threads < 0:
        raise Fail("The number of threads cannot be negative.")
    if not os.path.isfile(options.mix):
        raise Fail("The song file %s does not exist." % options.mix)

    # Better to find out now than after a minute of work that the result has nowhere to go.
    try:
        os.makedirs(options.out, exist_ok=True)
    except OSError as error:
        raise Fail("The folder %s for the stems could not be created." % options.out) from error
    if not os.access(options.out, os.W_OK):
        raise Fail("The folder %s for the stems cannot be written to." % options.out)

    use_nvidia_build_if_present(arguments)

    # The affinity has to be set before PyTorch starts its threads, which inherit it.
    threads = choose_threads(options.threads)
    # Operations that Metal does not implement yet fall back to the CPU instead of failing.
    os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
    os.environ.setdefault("HF_HUB_DISABLE_PROGRESS_BARS", "1")
    os.environ.setdefault("HF_HUB_DISABLE_TELEMETRY", "1")

    stage("reading audio")
    song, rate = read_song(options.mix)
    frames = len(song)
    mix = resample(song, rate, MODEL_RATE).T.copy()

    workers, inner, overlap = plan_cpu_work(mix.shape[1], threads)
    # torch.set_num_threads() alone is not enough: in the ARM build of PyTorch the convolutions
    # run in the Arm Compute Library, which sizes its thread pool from OpenMP's default when it
    # is loaded and kept using 8 threads here after PyTorch had been told to use one. The
    # environment variable is read by everything, so it is set before PyTorch is imported.
    os.environ["OMP_NUM_THREADS"] = str(inner)

    import torch

    torch.set_num_threads(inner)

    forced = os.environ.get("TENFOLD_STEMS_DEVICE")
    if forced:
        device = forced
    elif torch.cuda.is_available():
        device = "cuda"
    elif torch.backends.mps.is_available():
        device = "mps"
    else:
        device = "cpu"

    stems = None
    for attempt in ([device, "cpu"] if device != "cpu" else ["cpu"]):
        name = AUTO_MODEL[attempt] if options.model == "auto" else options.model
        model = load_model(name)
        stage("separating")
        try:
            stems = separate(model, mix, attempt, workers, overlap)
            break
        except Exception:
            if attempt == "cpu":
                raise
            # Whatever went wrong on the graphics card, the CPU can still do the job.
            traceback.print_exc()
            del model

    stage("writing")
    write_stems(stems, rate, frames, options.out)


def main():
    try:
        run(sys.argv[1:])
    except Fail as failure:
        if failure.__cause__ is not None:
            traceback.print_exception(failure.__cause__)
        die(str(failure))
    except MemoryError:
        die(OUT_OF_MEMORY)
    except SystemExit as stop:
        # argparse has already explained itself on standard error.
        if stop.code not in (0, None):
            die("The stem separator was started with arguments it does not understand.", 2)
        raise
    except BaseException as error:
        traceback.print_exc()
        # PyTorch reports a failed allocation as a plain RuntimeError.
        if "allocate" in str(error) or "out of memory" in str(error):
            die(OUT_OF_MEMORY)
        die("Stem separation failed unexpectedly (%s)." % type(error).__name__)
    sys.stdout.flush()
    sys.stderr.flush()
    # Leaving through os._exit skips the slow teardown of PyTorch's thread pools.
    os._exit(0)


if __name__ == "__main__":
    main()
