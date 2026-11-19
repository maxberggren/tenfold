#!/usr/bin/env python3
"""Look at a tenfold render without ears: level over time, how often the
limiter works, stereo width, brightness, and a log-frequency spectrogram.

    uv run --with numpy --with matplotlib tools/analyze.py out.wav [spectrogram.png]
"""
import sys, wave
import numpy as np

path = sys.argv[1]
with wave.open(path) as w:
    rate, n, ch, width = w.getframerate(), w.getnframes(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n)
b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
v = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / 8388608.0
x = v.reshape(-1, 2)
L, R = x[:, 0], x[:, 1]
mono = 0.5 * (L + R)
db = lambda a: 20 * np.log10(max(a, 1e-9))
print(f"{path}: {len(L)/rate:.1f}s  peak {db(np.abs(x).max()):.1f} dBFS  rms {db(np.sqrt((x**2).mean())):.1f} dBFS")
print(f"samples within 0.5 dB of the ceiling: {100*np.mean(np.abs(x) > 0.89*0.944):.3f}%")
print(" sec   rms   peak  corr  centroid  sub<80Hz  air>6k")
win = rate
for s in range(0, len(L) - win + 1, win):
    seg = x[s:s+win]
    m = mono[s:s+win]
    spec = np.abs(np.fft.rfft(m * np.hanning(win)))**2
    f = np.fft.rfftfreq(win, 1/rate)
    tot = spec.sum() + 1e-20
    cent = (spec * f).sum() / tot
    sub = spec[f < 80].sum() / tot
    air = spec[f > 6000].sum() / tot
    l, r = seg[:, 0], seg[:, 1]
    corr = np.corrcoef(l, r)[0, 1] if l.std() > 1e-7 and r.std() > 1e-7 else 1.0
    print(f"{s//rate:4d} {db(np.sqrt((seg**2).mean())):6.1f} {db(np.abs(seg).max()):6.1f} {corr:5.2f} {cent:8.0f} {sub:8.2f} {air:7.3f}")

if len(sys.argv) > 2:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    nfft, hop = 4096, 1024
    frames = 1 + (len(mono) - nfft) // hop
    idx = np.arange(nfft)[None, :] + hop * np.arange(frames)[:, None]
    S = np.abs(np.fft.rfft(mono[idx] * np.hanning(nfft), axis=1))
    S = 20 * np.log10(S + 1e-9)
    f = np.fft.rfftfreq(nfft, 1/rate)
    t = np.arange(frames) * hop / rate
    fig, ax = plt.subplots(figsize=(16, 6), dpi=90)
    ax.pcolormesh(t, f[1:], S[:, 1:].T, shading="auto", vmin=S.max()-90, vmax=S.max(), cmap="magma")
    ax.set_yscale("log"); ax.set_ylim(30, 20000)
    ax.set_xlabel("s"); ax.set_ylabel("Hz")
    fig.tight_layout(); fig.savefig(sys.argv[2])
    print("wrote", sys.argv[2])
