#include "audio.h"

#include <algorithm>

namespace {

const char* fileName(Sound sound) {
    switch (sound) {
        case Sound::BossDeath: return "bossdeath.wav";
        case Sound::Confirm: return "confirm.wav";
        case Sound::Craft: return "craft.wav";
        case Sound::Death: return "death.wav";
        case Sound::Explode: return "explode.wav";
        case Sound::Fuse: return "fuse.wav";
        case Sound::MonsterHurt: return "monsterhurt.wav";
        case Sound::Pickup: return "pickup.wav";
        case Sound::PlayerHurt: return "playerhurt.wav";
        case Sound::Select: return "select.wav";
    }
    return "";
}

}  // namespace

Audio::~Audio() {
    for (Clip& clip : clips_) {
        for (SDL_AudioStream* voice : clip.voices) {
            if (voice) SDL_DestroyAudioStream(voice);
        }
    }
    if (device_) SDL_CloseAudioDevice(device_);
}

bool Audio::init(const std::string& directory) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("No audio: %s", SDL_GetError());
        return false;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        SDL_Log("No audio device: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec deviceSpec{};
    SDL_GetAudioDeviceFormat(device_, &deviceSpec, nullptr);
    for (int i = 0; i < kSoundCount; ++i) {
        Clip& clip = clips_[static_cast<std::size_t>(i)];
        const std::string path = directory + fileName(static_cast<Sound>(i));
        Uint8* buffer = nullptr;
        Uint32 length = 0;
        if (!SDL_LoadWAV(path.c_str(), &clip.spec, &buffer, &length)) {
            SDL_Log("Failed to load %s: %s", path.c_str(), SDL_GetError());
            continue;
        }
        clip.samples.assign(buffer, buffer + length);
        SDL_free(buffer);
        // Each voice converts from the file's format to the device's and is mixed by the device.
        for (SDL_AudioStream*& voice : clip.voices) {
            voice = SDL_CreateAudioStream(&clip.spec, &deviceSpec);
            if (voice && !SDL_BindAudioStream(device_, voice)) {
                SDL_DestroyAudioStream(voice);
                voice = nullptr;
            }
        }
    }
    applyGain();
    return true;
}

void Audio::play(Sound sound) {
    if (!device_ || muted_ || volume_ == 0) return;
    Clip& clip = clips_[static_cast<std::size_t>(sound)];
    if (clip.samples.empty()) return;
    // Round-robin over the voices: a fifth overlapping copy cuts off the oldest one.
    SDL_AudioStream* voice = clip.voices[static_cast<std::size_t>(clip.nextVoice)];
    clip.nextVoice = (clip.nextVoice + 1) % kVoicesPerSound;
    if (!voice) return;
    SDL_ClearAudioStream(voice);
    SDL_PutAudioStreamData(voice, clip.samples.data(), static_cast<int>(clip.samples.size()));
}

void Audio::setMuted(bool muted) {
    muted_ = muted;
    applyGain();
}

void Audio::setVolume(int volume) {
    volume_ = std::clamp(volume, 0, kVolumeSteps);
    applyGain();
}

void Audio::applyGain() {
    const float gain = muted_ ? 0.0f : static_cast<float>(volume_) / static_cast<float>(kVolumeSteps);
    for (Clip& clip : clips_) {
        for (SDL_AudioStream* voice : clip.voices) {
            if (voice) SDL_SetAudioStreamGain(voice, gain);
        }
    }
}
