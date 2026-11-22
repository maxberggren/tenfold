#include "coreaudio.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "dsp.h"
#include "microphone.h"
#include "studio.h"

namespace tf {

namespace {

constexpr UInt32 kBufferFrames = 128;
constexpr UInt32 kMaxFrames = 8192;

AudioObjectID defaultDevice(bool input) {
    AudioObjectPropertyAddress where{input ? kAudioHardwarePropertyDefaultInputDevice
                                           : kAudioHardwarePropertyDefaultOutputDevice,
                                     kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &where, 0, nullptr, &size, &device) != noErr)
        return kAudioObjectUnknown;
    return device;
}

UInt32 deviceNumber(AudioObjectID device, AudioObjectPropertySelector what, bool input) {
    AudioObjectPropertyAddress where{what, input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                     kAudioObjectPropertyElementMain};
    UInt32 value = 0, size = sizeof(value);
    if (AudioObjectGetPropertyData(device, &where, 0, nullptr, &size, &value) != noErr)
        return 0;
    return value;
}

double deviceRate(AudioObjectID device) {
    AudioObjectPropertyAddress where{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    Float64 rate = 0;
    UInt32 size = sizeof(rate);
    if (AudioObjectGetPropertyData(device, &where, 0, nullptr, &size, &rate) != noErr)
        return 0;
    return rate;
}

std::string deviceName(AudioObjectID device) {
    AudioObjectPropertyAddress where{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain};
    CFStringRef name = nullptr;
    UInt32 size = sizeof(name);
    if (AudioObjectGetPropertyData(device, &where, 0, nullptr, &size, &name) != noErr || !name)
        return "?";
    char buffer[256] = {};
    CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(name);
    return buffer;
}

// What the device adds to the buffer itself, each way: its own latency,
// its safety offset and its buffer, in milliseconds.
float deviceMs(AudioObjectID device, bool input) {
    const double rate = deviceRate(device);
    if (rate <= 0)
        return 0;
    const UInt32 frames = deviceNumber(device, kAudioDevicePropertyLatency, input) +
                          deviceNumber(device, kAudioDevicePropertySafetyOffset, input) +
                          deviceNumber(device, kAudioDevicePropertyBufferFrameSize, input);
    return float(1000.0 * frames / rate);
}

OSStatus onRender(void *self, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32 frames,
                  AudioBufferList *io) {
    if (io && io->mNumberBuffers > 0 && io->mBuffers[0].mData)
        static_cast<CoreAudioOut *>(self)->render(static_cast<float *>(io->mBuffers[0].mData), int(frames));
    return noErr;
}

OSStatus onCapture(void *self, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *timestamp, UInt32,
                   UInt32 frames, AudioBufferList *) {
    static_cast<CoreAudioOut *>(self)->capture(flags, timestamp, frames);
    return noErr;
}

AudioStreamBasicDescription floatFormat(double rate, UInt32 channels) {
    AudioStreamBasicDescription f{};
    f.mSampleRate = rate;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    f.mChannelsPerFrame = channels;
    f.mBitsPerChannel = 32;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4 * channels;
    f.mBytesPerPacket = 4 * channels;
    return f;
}

}

CoreAudioOut::CoreAudioOut(Studio *studio) : m_studio(studio), m_rate(studio->sampleRate()) {}

CoreAudioOut::~CoreAudioOut() { stop(); }

std::string CoreAudioOut::inputError() const {
    std::lock_guard lock(m_errorLock);
    return m_inputError;
}

bool CoreAudioOut::start() {
    if (m_output)
        return true;
    AudioComponentDescription what{kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &what);
    AudioUnit unit = nullptr;
    if (!component || AudioComponentInstanceNew(component, &unit) != noErr) {
        m_error = "Cannot open the sound output.";
        return false;
    }
    // The studio's own rate in, interleaved stereo: the unit converts to
    // whatever the device runs at.
    AudioStreamBasicDescription format = floatFormat(m_rate, 2);
    AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof(format));
    AURenderCallbackStruct callback{onRender, this};
    AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                         sizeof(callback));
    UInt32 maxFrames = kMaxFrames;
    AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames,
                         sizeof(maxFrames));
    // The smallest buffer the device takes, so a finger is heard at once.
    UInt32 frames = kBufferFrames;
    AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames,
                         sizeof(frames));
    if (AudioUnitInitialize(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        m_error = "Cannot start the sound output.";
        return false;
    }
    m_output = unit;
    if (const AudioObjectID out = defaultDevice(false); out != kAudioObjectUnknown)
        m_outMs = deviceMs(out, false);

    if (!std::getenv("TENFOLD_NO_MIC") && !startInput()) {
        m_inputFailed = true;
        m_studio->setInput(StudioView::InputFailed);
    }
    if (AudioOutputUnitStart(unit) != noErr) {
        m_error = "Cannot start the sound output.";
        stop();
        return false;
    }
    m_running = true;
    return true;
}

bool CoreAudioOut::startInput() {
    auto fail = [this](const char *why) {
        std::lock_guard lock(m_errorLock);
        m_inputError = why;
        return false;
    };
    if (microphoneDenied())
        return fail("tenfold may not use the microphone: allow it in System Settings > Privacy & Security > Microphone");
    askForMicrophone();
    const AudioObjectID device = defaultDevice(true);
    if (device == kAudioObjectUnknown)
        return fail("there is no microphone");
    AudioComponentDescription what{kAudioUnitType_Output, kAudioUnitSubType_HALOutput, kAudioUnitManufacturer_Apple,
                                   0, 0};
    AudioComponent component = AudioComponentFindNext(nullptr, &what);
    AudioUnit unit = nullptr;
    if (!component || AudioComponentInstanceNew(component, &unit) != noErr)
        return fail("the microphone won't open");
    UInt32 on = 1, off = 0;
    AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &on, sizeof(on));
    AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &off, sizeof(off));
    AudioObjectID current = device;
    if (AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &current,
                             sizeof(current)) != noErr) {
        AudioComponentInstanceDispose(unit);
        return fail("the microphone won't open");
    }
    // The input unit doesn't change the rate, only the format: mono floats
    // at the device's own rate, converted to the studio's here.
    m_inRate = deviceRate(device);
    if (m_inRate <= 0)
        m_inRate = m_rate;
    AudioStreamBasicDescription format = floatFormat(m_inRate, 1);
    AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &format, sizeof(format));
    AURenderCallbackStruct callback{onCapture, this};
    AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback,
                         sizeof(callback));
    UInt32 frames = kBufferFrames;
    AudioUnitSetProperty(unit, kAudioDevicePropertyBufferFrameSize, kAudioUnitScope_Global, 0, &frames,
                         sizeof(frames));
    m_in.assign(kMaxFrames, 0.0f);
    m_out.assign(size_t(kMaxFrames * (double(m_rate) / m_inRate) + 4), 0.0f);
    if (AudioUnitInitialize(unit) != noErr) {
        AudioComponentInstanceDispose(unit);
        return fail("the microphone won't start");
    }
    m_inMs = deviceMs(device, true);
    m_studio->setInput(StudioView::InputConnecting, m_inMs, m_outMs);
    if (AudioOutputUnitStart(unit) != noErr) {
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        return fail("the microphone won't start");
    }
    m_input = unit;
    return true;
}

void CoreAudioOut::stop() {
    if (m_input) {
        auto unit = static_cast<AudioUnit>(m_input);
        AudioOutputUnitStop(unit);
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        m_input = nullptr;
    }
    if (m_output) {
        auto unit = static_cast<AudioUnit>(m_output);
        AudioOutputUnitStop(unit);
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        m_output = nullptr;
    }
    m_running = false;
}

void CoreAudioOut::render(float *interleaved, int frames) {
    if (!m_flushed) {
        flushDenormals();
        m_flushed = true;
    }
    if (!m_clockSet) {
        // The engine's clock starts now, on the clock the trackpad's
        // frames are stamped with.
        m_studio->setTimeOrigin(monotonicSeconds());
        m_clockSet = true;
    }
    // The microphone is live while it keeps delivering: a second without a
    // sample and the page says so.
    if (m_input) {
        const uint64_t got = m_captured.load(std::memory_order_acquire);
        if (got != m_seen) {
            m_seen = got;
            m_quiet = 0;
        } else {
            m_quiet += uint64_t(frames);
        }
        const int state = m_inputFailed.load(std::memory_order_relaxed) ? StudioView::InputFailed
                          : m_quiet < uint64_t(m_rate) && got > 0      ? StudioView::InputLive
                                                                        : StudioView::InputConnecting;
        m_studio->setInput(state, m_inMs, m_outMs);
    }
    m_studio->render(interleaved, frames);
    m_quantum.store(frames, std::memory_order_relaxed);
}

void CoreAudioOut::capture(void *flags, const void *timestamp, unsigned frames) {
    if (!m_input || frames == 0 || frames > m_in.size())
        return;
    AudioBufferList list{};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = 1;
    list.mBuffers[0].mDataByteSize = frames * sizeof(float);
    list.mBuffers[0].mData = m_in.data();
    if (AudioUnitRender(static_cast<AudioUnit>(m_input), static_cast<AudioUnitRenderActionFlags *>(flags),
                        static_cast<const AudioTimeStamp *>(timestamp), 1, frames, &list) != noErr)
        return;
    const int n = int(frames);
    if (std::fabs(m_inRate - double(m_rate)) < 0.5) {
        m_studio->pushInput(m_in.data(), n);
    } else {
        // Linear interpolation between the device's samples, carrying the
        // position and the last sample from one buffer to the next.
        const double step = m_inRate / double(m_rate);
        double pos = m_phase;
        int made = 0;
        while (pos <= double(n - 1) && made < int(m_out.size())) {
            const int i = int(std::floor(pos));
            const float frac = float(pos - i);
            const float a = i < 0 ? m_last : m_in[size_t(i)];
            const float b = i + 1 < n ? m_in[size_t(i + 1)] : a;
            m_out[size_t(made++)] = a + (b - a) * frac;
            pos += step;
        }
        m_phase = pos - double(n);
        m_last = m_in[size_t(n - 1)];
        if (made > 0)
            m_studio->pushInput(m_out.data(), made);
    }
    m_captured.fetch_add(frames, std::memory_order_release);
}

int reportCoreAudioLatency(bool json) {
    const AudioObjectID out = defaultDevice(false), in = defaultDevice(true);
    const float outMs = out != kAudioObjectUnknown ? deviceMs(out, false) : 0;
    const float inMs = in != kAudioObjectUnknown ? deviceMs(in, true) : 0;
    const double outRate = out != kAudioObjectUnknown ? deviceRate(out) : 0;
    if (json) {
        std::printf("{\n  \"output\": \"%s\",\n  \"output_ms\": %.2f,\n  \"input\": \"%s\",\n  \"input_ms\": %.2f,\n"
                    "  \"rate\": %.0f\n}\n",
                    out != kAudioObjectUnknown ? deviceName(out).c_str() : "", outMs,
                    in != kAudioObjectUnknown ? deviceName(in).c_str() : "", inMs, outRate);
    } else {
        std::printf("output   %s, %.2f ms at %.0f Hz (latency, safety offset and buffer)\n",
                    out != kAudioObjectUnknown ? deviceName(out).c_str() : "none", outMs, outRate);
        std::printf("input    %s, %.2f ms\n", in != kAudioObjectUnknown ? deviceName(in).c_str() : "none", inMs);
        std::printf("so, from the mic to that output, about %.1f ms plus tenfold's own buffer\n", outMs + inMs);
    }
    return out != kAudioObjectUnknown ? 0 : 1;
}

}
