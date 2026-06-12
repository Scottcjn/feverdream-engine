// SPDX-License-Identifier: MIT
// fd_audio.h -- Feverdream Engine: procedural audio for the game host.
//
// The .kkrieger rule applied to sound: no asset files, no decoder libs, no
// SDL2_mixer. Every effect is SYNTHESIZED at init from a tiny recipe
// (oscillator + noise + envelope), then played through an 8-voice mixer in
// the SDL2 audio callback. Total cost: this header.
//
// Audio is optional at runtime but testable headless: play() counts trigger
// events even when no audio device opened (selftest asserts the gameplay
// hooks fired; speakers are not a build dependency).

#ifndef FD_AUDIO_H
#define FD_AUDIO_H

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <SDL2/SDL.h>

class FdAudio {
public:
    enum Sound { JUMP, LAND, STEP, BUMP, BLIP, SOUND_COUNT };

    // synthesize all effects, then try to open a device; silent failure is OK
    bool init() {
        synth_all();
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1;
        want.samples = 512;            // ~23ms latency at 22050Hz
        want.callback = &FdAudio::sdl_callback;
        want.userdata = this;
        dev_ = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (dev_ == 0) return false;
        SDL_PauseAudioDevice(dev_, 0);
        return true;
    }

    void shutdown() {
        if (dev_) { SDL_CloseAudioDevice(dev_); dev_ = 0; }
    }

    // trigger a sound; counts the event even with no device (headless test)
    void play(Sound s, float gain = 1.0f) {
        if (s < 0 || s >= SOUND_COUNT) return;
        triggers[s]++;
        if (!dev_) return;
        SDL_LockAudioDevice(dev_);
        // steal the oldest finished (or oldest, period) voice
        int pick = 0;
        for (int i = 0; i < NVOICES; ++i) {
            if (!voices_[i].active) { pick = i; break; }
            if (voices_[i].pos > voices_[pick].pos) pick = i;
        }
        voices_[pick].samples = &buf_[s];
        voices_[pick].pos = 0;
        voices_[pick].gain = gain;
        voices_[pick].active = true;
        SDL_UnlockAudioDevice(dev_);
    }

    bool device_ok() const { return dev_ != 0; }
    long triggers[SOUND_COUNT] = {0};   // per-sound trigger counts (always)

private:
    static const int RATE = 22050;
    static const int NVOICES = 8;

    struct Voice {
        const std::vector<int16_t>* samples = NULL;
        size_t pos = 0;
        float gain = 1.0f;
        bool active = false;
    };

    SDL_AudioDeviceID dev_ = 0;
    std::vector<int16_t> buf_[SOUND_COUNT];
    Voice voices_[NVOICES];
    uint32_t noise_ = 0x46445541;      // deterministic noise seed ("FDAU")

    float noise() {                    // xorshift, [-1,1)
        noise_ ^= noise_ << 13; noise_ ^= noise_ >> 17; noise_ ^= noise_ << 5;
        return (int32_t)noise_ / 2147483648.0f;
    }

    // recipe: duration, pitch sweep f0->f1 (Hz), waveform mix, decay shape
    void synth(Sound s, float secs, float f0, float f1,
               float tone_amp, float noise_amp, float punch) {
        std::vector<int16_t>& b = buf_[s];
        size_t n = (size_t)(secs * RATE);
        b.resize(n);
        float phase = 0;
        for (size_t i = 0; i < n; ++i) {
            float t = (float)i / n;                       // 0..1
            float f = f0 + (f1 - f0) * t;                 // linear sweep
            phase += 2.0f * (float)M_PI * f / RATE;
            float env = powf(1.0f - t, punch);            // punch>1 = snappier
            float v = tone_amp * sinf(phase) + noise_amp * noise();
            float clipped = fmaxf(-1.0f, fminf(1.0f, v * env));
            b[i] = (int16_t)(clipped * 28000);
        }
    }

    void synth_all() {
        //          secs   f0    f1   tone  noise punch
        synth(JUMP, 0.18f, 220,  520, 0.7f, 0.0f, 1.5f);  // rising chirp
        synth(LAND, 0.12f,  90,   45, 0.8f, 0.3f, 2.5f);  // low thud + grit
        synth(STEP, 0.04f, 150,  110, 0.2f, 0.5f, 3.0f);  // short noisy tick
        synth(BUMP, 0.09f, 130,   70, 0.6f, 0.4f, 2.2f);  // wall knock
        synth(BLIP, 0.10f, 660,  880, 0.6f, 0.0f, 2.0f);  // script-triggerable
    }

    void mix(int16_t* out, int count) {
        memset(out, 0, count * sizeof(int16_t));
        for (int v = 0; v < NVOICES; ++v) {
            Voice& vo = voices_[v];
            if (!vo.active) continue;
            for (int i = 0; i < count && vo.pos < vo.samples->size(); ++i, ++vo.pos) {
                int32_t mixed = out[i] + (int32_t)((*vo.samples)[vo.pos] * vo.gain);
                out[i] = (int16_t)(mixed > 32767 ? 32767 : mixed < -32768 ? -32768 : mixed);
            }
            if (vo.pos >= vo.samples->size()) vo.active = false;
        }
    }

    static void sdl_callback(void* userdata, Uint8* stream, int len) {
        ((FdAudio*)userdata)->mix((int16_t*)stream, len / 2);
    }
};

#endif
