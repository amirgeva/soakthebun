#include "app/audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace soak {
namespace {

constexpr float kTwoPi = 6.28318530718f;

struct Noise {
    uint32_t s = 0x12345678u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / float(1u << 23) - 1.f;
    }
};

}  // namespace

bool Audio::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device_) {
        SDL_Log("No audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    rate_ = have.freq;
    synthesize();
    voices_.reserve(32);
    SDL_PauseAudioDevice(device_, 0);
    return true;
}

void Audio::shutdown() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void Audio::play(Sfx sfx) {
    if (!device_ || muted_) return;
    SDL_LockAudioDevice(device_);
    if (voices_.size() < 24) voices_.push_back(Voice{int(sfx), 0});
    SDL_UnlockAudioDevice(device_);
}

void SDLCALL Audio::callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<Audio*>(user);
    float* out = reinterpret_cast<float*>(stream);
    const size_t frames = size_t(len) / sizeof(float);
    std::fill(out, out + frames, 0.f);
    for (size_t v = 0; v < self->voices_.size();) {
        Voice& voice = self->voices_[v];
        const std::vector<float>& snd = self->sounds_[voice.sound];
        const size_t n = std::min(frames, snd.size() - voice.pos);
        for (size_t i = 0; i < n; ++i) out[i] += snd[voice.pos + i];
        voice.pos += n;
        if (voice.pos >= snd.size()) {
            self->voices_[v] = self->voices_.back();
            self->voices_.pop_back();
        } else {
            ++v;
        }
    }
    for (size_t i = 0; i < frames; ++i) out[i] = std::clamp(out[i], -1.f, 1.f);
}

void Audio::synthesize() {
    const float rate = float(rate_);
    auto make = [&](float seconds) { return std::vector<float>(size_t(seconds * rate)); };
    Noise noise;

    {  // Push: a soft, low thump.
        auto& s = sounds_[int(Sfx::Push)] = make(0.10f);
        float phase = 0.f;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            phase += kTwoPi * (120.f - 400.f * t) / rate;
            s[i] = (std::sin(phase) * 0.8f + noise.next() * 0.25f * std::exp(-t * 90.f)) * std::exp(-t * 32.f) * 0.32f;
        }
    }
    {  // Splash: filtered noise burst plus a few rising "bloops".
        auto& s = sounds_[int(Sfx::Splash)] = make(0.55f);
        float lp = 0.f;
        const float bloopStart[3] = {0.02f, 0.09f, 0.17f};
        const float bloopBase[3] = {380.f, 520.f, 450.f};
        float bloopPhase[3] = {0.f, 0.f, 0.f};
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            const float cutoff = 0.05f + 0.45f * std::exp(-t * 9.f);
            lp += (noise.next() - lp) * cutoff;
            float v = lp * std::exp(-t * 7.f) * std::min(1.f, t * 300.f) * 0.9f;
            for (int b = 0; b < 3; ++b) {
                const float bt = t - bloopStart[b];
                if (bt < 0.f || bt > 0.12f) continue;
                bloopPhase[b] += kTwoPi * (bloopBase[b] + 5200.f * bt) / rate;
                v += std::sin(bloopPhase[b]) * std::exp(-bt * 30.f) * 0.35f;
            }
            s[i] = v * 0.45f;
        }
    }
    {  // Undo: a quick downward blip.
        auto& s = sounds_[int(Sfx::Undo)] = make(0.07f);
        float phase = 0.f;
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            phase += kTwoPi * (620.f - 3000.f * t) / rate;
            s[i] = std::sin(phase) * std::exp(-t * 40.f) * 0.14f;
        }
    }
    {  // Tick: menu navigation.
        auto& s = sounds_[int(Sfx::Tick)] = make(0.035f);
        for (size_t i = 0; i < s.size(); ++i) {
            const float t = float(i) / rate;
            s[i] = std::sin(kTwoPi * 1250.f * t) * std::exp(-t * 140.f) * 0.10f;
        }
    }
    {  // Win: a bright rising arpeggio.
        auto& s = sounds_[int(Sfx::Win)] = make(1.1f);
        const float notes[5] = {523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f};
        for (int n = 0; n < 5; ++n) {
            const float start = 0.09f * float(n);
            for (size_t i = size_t(start * rate); i < s.size(); ++i) {
                const float t = float(i) / rate - start;
                const float env = std::exp(-t * (n == 4 ? 3.5f : 7.f)) * std::min(1.f, t * 400.f);
                const float ph = kTwoPi * notes[n] * t;
                s[i] += (std::sin(ph) + 0.3f * std::sin(2.f * ph)) * env * 0.14f;
            }
        }
    }
}

}  // namespace soak
