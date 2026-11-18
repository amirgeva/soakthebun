#pragma once

#include <SDL.h>

#include <vector>

namespace soak {

enum class Sfx { Push, Splash, Undo, Win, Tick, Count };

// A few synthesized sound effects mixed in the SDL audio callback. If no
// audio device is available the game simply runs silently.
class Audio {
public:
    ~Audio() { shutdown(); }

    bool init();
    void shutdown();
    void play(Sfx sfx);
    void setMuted(bool muted) { muted_ = muted; }
    bool muted() const { return muted_; }

private:
    static void SDLCALL callback(void* user, Uint8* stream, int len);
    void synthesize();

    struct Voice {
        int sound;
        size_t pos;
    };

    SDL_AudioDeviceID device_ = 0;
    int rate_ = 44100;
    std::vector<float> sounds_[int(Sfx::Count)];
    std::vector<Voice> voices_;  // guarded by SDL_LockAudioDevice
    bool muted_ = false;
};

}  // namespace soak
