#pragma once
#include "nd/core/Base.h"

namespace nd {

enum class AudioBus { Master, Music, SFX };

struct SoundParams {
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool spatial = false;
    vec3 position{ 0.0f };
    float minDistance = 1.0f;
    float maxDistance = 40.0f;
    AudioBus bus = AudioBus::SFX;
    bool stream = false;           // stream from disk (music)
};

// Audio engine built on miniaudio: 2D/3D sounds, music streaming, buses with volume,
// a listener that follows the camera or an AudioListener component.
// Formats: WAV, MP3, FLAC.
class Audio {
public:
    static void Init();
    static void Shutdown();
    static void Update();                         // cleans up finished one-shots
    static bool Available() { return s_Available; }

    static uint32_t Play(const std::string& path, const SoundParams& params = {});
    static void PlayOneShot(const std::string& path, float volume = 1.0f);
    static void PlayOneShotAt(const std::string& path, const vec3& position, float volume = 1.0f);
    static void Stop(uint32_t handle);
    static void StopAll();
    static bool IsPlaying(uint32_t handle);
    static void SetVolume(uint32_t handle, float volume);
    static void SetPitch(uint32_t handle, float pitch);
    static void SetPosition(uint32_t handle, const vec3& position);

    static void SetListener(const vec3& position, const vec3& forward, const vec3& up = { 0, 1, 0 });
    static void SetBusVolume(AudioBus bus, float volume);
    static float GetBusVolume(AudioBus bus);

private:
    static bool s_Available;
};

} // namespace nd
