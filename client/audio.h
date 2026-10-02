#pragma once

#include "events.h"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

// Plays the sound effects through SDL3 audio (Minicraft's Sound.play). Every sound has a few audio streams bound to
// the playback device, so the same effect can overlap itself; the device mixes them. Muting and the volume apply
// to every stream. If no audio device can be opened the game simply stays silent.
class Audio {
public:
    static constexpr int kVolumeSteps = 10;  // volume goes 0..10 (0 % to 100 %)

    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the default playback device and loads <directory><name>.wav for every Sound. Returns false (and logs)
    // if audio is unavailable; the game keeps running without sound.
    bool init(const std::string& directory);

    void play(Sound sound);

    bool muted() const { return muted_; }
    void setMuted(bool muted);
    void toggleMuted() { setMuted(!muted_); }
    int volume() const { return volume_; }
    void setVolume(int volume);

private:
    static constexpr int kVoicesPerSound = 4;

    struct Clip {
        SDL_AudioSpec spec{};
        std::vector<Uint8> samples;
        std::array<SDL_AudioStream*, kVoicesPerSound> voices{};
        int nextVoice = 0;
    };

    void applyGain();

    SDL_AudioDeviceID device_ = 0;
    std::array<Clip, kSoundCount> clips_{};
    bool muted_ = false;
    int volume_ = 7;
};
