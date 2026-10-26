#include "nd/audio/Audio.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"
#include <miniaudio.h>
#include <map>

namespace nd {

bool Audio::s_Available = false;

namespace {
struct SoundEntry {
    ma_sound* sound = nullptr;
    bool oneShot = false;
};
ma_engine* s_Engine = nullptr;
ma_sound_group s_Music, s_SFX;
std::map<uint32_t, SoundEntry> s_Sounds;
uint32_t s_NextHandle = 1;
float s_Volumes[3] = { 1.0f, 0.8f, 1.0f };

ma_sound_group* Group(AudioBus bus) {
    if (bus == AudioBus::Music) return &s_Music;
    if (bus == AudioBus::SFX) return &s_SFX;
    return nullptr;
}

void Destroy(SoundEntry& e) {
    if (!e.sound) return;
    ma_sound_uninit(e.sound);
    delete e.sound;
    e.sound = nullptr;
}
} // namespace

void Audio::Init() {
    s_Engine = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, s_Engine) != MA_SUCCESS) {
        ND_WARN("No audio device available - sound is disabled");
        delete s_Engine;
        s_Engine = nullptr;
        return;
    }
    ma_sound_group_init(s_Engine, 0, nullptr, &s_Music);
    ma_sound_group_init(s_Engine, 0, nullptr, &s_SFX);
    ma_sound_group_set_volume(&s_Music, s_Volumes[1]);
    s_Available = true;
    ND_INFO("Audio initialized ({} Hz)", ma_engine_get_sample_rate(s_Engine));
}

void Audio::Shutdown() {
    if (!s_Engine) return;
    for (auto& [h, e] : s_Sounds) Destroy(e);
    s_Sounds.clear();
    ma_sound_group_uninit(&s_Music);
    ma_sound_group_uninit(&s_SFX);
    ma_engine_uninit(s_Engine);
    delete s_Engine;
    s_Engine = nullptr;
    s_Available = false;
}

void Audio::Update() {
    if (!s_Available) return;
    for (auto it = s_Sounds.begin(); it != s_Sounds.end();) {
        if (it->second.oneShot && ma_sound_at_end(it->second.sound)) {
            Destroy(it->second);
            it = s_Sounds.erase(it);
        } else ++it;
    }
}

uint32_t Audio::Play(const std::string& path, const SoundParams& p) {
    if (!s_Available || path.empty()) return 0;
    std::string full = Project::Resolve(path).string();
    auto* sound = new ma_sound();
    ma_uint32 flags = p.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (!p.spatial) flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(s_Engine, full.c_str(), flags, Group(p.bus), nullptr, sound) != MA_SUCCESS) {
        ND_WARN("Could not play sound '{}'", path);
        delete sound;
        return 0;
    }
    ma_sound_set_volume(sound, p.volume);
    ma_sound_set_pitch(sound, p.pitch);
    ma_sound_set_looping(sound, p.loop ? MA_TRUE : MA_FALSE);
    if (p.spatial) {
        ma_sound_set_position(sound, p.position.x, p.position.y, p.position.z);
        ma_sound_set_min_distance(sound, p.minDistance);
        ma_sound_set_max_distance(sound, p.maxDistance);
        ma_sound_set_attenuation_model(sound, ma_attenuation_model_inverse);
    }
    ma_sound_start(sound);
    uint32_t h = s_NextHandle++;
    s_Sounds[h] = { sound, false };
    return h;
}

void Audio::PlayOneShot(const std::string& path, float volume) {
    SoundParams p;
    p.volume = volume;
    uint32_t h = Play(path, p);
    if (h) s_Sounds[h].oneShot = true;
}

void Audio::PlayOneShotAt(const std::string& path, const vec3& pos, float volume) {
    SoundParams p;
    p.volume = volume;
    p.spatial = true;
    p.position = pos;
    uint32_t h = Play(path, p);
    if (h) s_Sounds[h].oneShot = true;
}

void Audio::Stop(uint32_t h) {
    auto it = s_Sounds.find(h);
    if (it == s_Sounds.end()) return;
    Destroy(it->second);
    s_Sounds.erase(it);
}

void Audio::StopAll() {
    for (auto& [h, e] : s_Sounds) Destroy(e);
    s_Sounds.clear();
}

bool Audio::IsPlaying(uint32_t h) {
    auto it = s_Sounds.find(h);
    return it != s_Sounds.end() && ma_sound_is_playing(it->second.sound);
}

void Audio::SetVolume(uint32_t h, float v) { auto it = s_Sounds.find(h); if (it != s_Sounds.end()) ma_sound_set_volume(it->second.sound, v); }
void Audio::SetPitch(uint32_t h, float v) { auto it = s_Sounds.find(h); if (it != s_Sounds.end()) ma_sound_set_pitch(it->second.sound, v); }
void Audio::SetPosition(uint32_t h, const vec3& p) { auto it = s_Sounds.find(h); if (it != s_Sounds.end()) ma_sound_set_position(it->second.sound, p.x, p.y, p.z); }

void Audio::SetListener(const vec3& pos, const vec3& fwd, const vec3& up) {
    if (!s_Available) return;
    ma_engine_listener_set_position(s_Engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(s_Engine, 0, fwd.x, fwd.y, fwd.z);
    ma_engine_listener_set_world_up(s_Engine, 0, up.x, up.y, up.z);
}

void Audio::SetBusVolume(AudioBus bus, float v) {
    s_Volumes[(int)bus] = v;
    if (!s_Available) return;
    if (bus == AudioBus::Master) ma_engine_set_volume(s_Engine, v);
    else ma_sound_group_set_volume(Group(bus), v);
}

float Audio::GetBusVolume(AudioBus bus) { return s_Volumes[(int)bus]; }

} // namespace nd
