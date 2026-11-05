// BattleZone: an arena encounter. Walking into the trigger seals the area (red barriers come
// up), battle music starts and waves of enemies crawl out of the ground at the spawn points.
// When the last wave falls the seals drop and the fight is graded: time, damage taken and
// the best style rank you reached.
#include "Combat.h"
#include "PlayerCombat.h"
#include "PlayerController.h"
#include "GameManager.h"
#include <sstream>

using namespace eu;

class BattleZone : public ScriptableEntity {
public:
    std::string title = "AMBUSH";
    std::string waves = "3,4";          // regular enemies per wave
    std::string heavies = "0,1";        // heavy enemies per wave
    uint64_t enemyTemplate = 0;         // inactive enemy to copy
    uint64_t heavyTemplate = 0;
    std::string sealTag = "Seal";
    std::string spawnTag = "EnemySpawn";
    std::string music = "Audio/battle_music.wav";

    void Reflect(FieldVisitor& v) override {
        v.String("Title", title);
        v.String("Waves", waves);
        v.String("Heavies", heavies);
        v.EntityRef("Enemy Template", enemyTemplate);
        v.EntityRef("Heavy Template", heavyTemplate);
        v.String("Seal Tag", sealTag);
        v.String("Spawn Tag", spawnTag);
        v.String("Music", music);
    }

    void OnCreate() override {
        m_Done = SaveGame::GetBool(Key());
        SetSeals(false);
    }

    void OnDestroy() override {
        if (m_Music) Audio::Stop(m_Music);
    }

    void OnTriggerEnter(Entity other) override {
        if (m_Active || m_Done || other.Get<TagComponent>().tag != "Player") return;
        Begin();
    }

    void OnUpdate(float dt) override {
        float real = Time::UnscaledDelta();
        m_Banner -= real;
        m_Result -= real;
        // Music fades in during the fight and out after it
        float want = m_Active ? 0.55f : 0.0f;
        m_MusicVolume += glm::clamp(want - m_MusicVolume, -real * 0.4f, real * 1.5f);
        if (m_Music) {
            Audio::SetVolume(m_Music, m_MusicVolume);
            if (!m_Active && m_MusicVolume <= 0.0f) { Audio::Stop(m_Music); m_Music = 0; }
        }
        if (!m_Active) return;

        m_Elapsed += dt;
        if (auto* pc = PlayerCombat::Local()) m_BestRank = std::max(m_BestRank, pc->Style().Rank());
        int alive = 0;
        for (uint64_t id : m_Spawned) {
            Entity e = FindByUUID(id);
            auto* c = e ? GetScriptOn<Combatant>(e) : nullptr;
            if (c && !c->IsDead()) alive++;
        }
        if (alive > 0) { m_NextWave = 1.2f; return; }
        m_NextWave -= dt;
        if (m_NextWave > 0.0f) return;
        if (m_Wave + 1 < (int)Counts(waves).size()) SpawnWave(m_Wave + 1);
        else Finish();
    }

    void OnGUI() override {
        vec2 c = UI::CanvasSize();
        if (m_Banner > 0.0f) {
            float a = glm::clamp(m_Banner, 0.0f, 1.0f) * glm::clamp((2.4f - m_Banner) * 4.0f, 0.0f, 1.0f);
            UI::Panel({ { 0, c.y * 0.42f }, { c.x, 120 } }, { 0.35f, 0.0f, 0.02f, 0.55f * a });
            UI::Text({ c.x * 0.5f, c.y * 0.42f + 18 }, m_BannerText, 80, { 1.0f, 0.85f, 0.8f, a }, UI::Align::Center);
        }
        if (m_Active && m_Wave >= 0) {
            int n = (int)Counts(waves).size();
            UI::Text({ c.x * 0.5f, 40 }, title + "   -   WAVE " + std::to_string(m_Wave + 1) + " / " + std::to_string(n), 30, { 1, 0.6f, 0.55f, 0.9f },
                     UI::Align::Center);
        }
        if (m_Result > 0.0f) {
            float a = glm::clamp(m_Result, 0.0f, 1.0f) * glm::clamp((7.0f - m_Result) * 3.0f, 0.0f, 1.0f);
            vec2 at(c.x * 0.5f - 300, c.y * 0.24f);
            UI::Panel({ at, { 600, 330 } }, { 0, 0, 0, 0.7f * a });
            UI::Text({ c.x * 0.5f, at.y + 22 }, "AREA CLEAR", 64, { 1, 1, 1, a }, UI::Align::Center);
            int secs = (int)m_Elapsed;
            std::string time = std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60);
            UI::Text({ at.x + 40, at.y + 120 }, "Time", 32, { 1, 1, 1, 0.8f * a });
            UI::Text({ at.x + 560, at.y + 120 }, time, 32, { 1, 1, 1, a }, UI::Align::Right);
            UI::Text({ at.x + 40, at.y + 165 }, "Damage taken", 32, { 1, 1, 1, 0.8f * a });
            UI::Text({ at.x + 560, at.y + 165 }, std::to_string((int)m_DamageTaken), 32, { 1, 1, 1, a }, UI::Align::Right);
            UI::Text({ at.x + 40, at.y + 210 }, "Best style", 32, { 1, 1, 1, 0.8f * a });
            UI::Text({ at.x + 560, at.y + 210 }, m_BestRank >= 0 ? combat::StyleMeter::Letter(m_BestRank) : "-", 32,
                     m_BestRank >= 0 ? combat::StyleMeter::Color(m_BestRank) * vec4(1, 1, 1, a) : vec4(1, 1, 1, a), UI::Align::Right);
            UI::Text({ at.x + 40, at.y + 262 }, "Rank", 40, { 1, 0.85f, 0.4f, a });
            UI::Text({ at.x + 560, at.y + 250 }, m_Grade, 60, combat::StyleMeter::Color(m_GradeRank) * vec4(1, 1, 1, a), UI::Align::Right);
        }
    }

private:
    std::string Key() { return "zone_cleared:" + std::to_string((uint64_t)GetEntity().GetUUID()); }

    static std::vector<int> Counts(const std::string& list) {
        std::vector<int> out;
        std::stringstream ss(list);
        std::string item;
        while (std::getline(ss, item, ',')) out.push_back(std::max(0, std::atoi(item.c_str())));
        return out;
    }

    void SetSeals(bool on) {
        for (Entity e : GetScene().FindAllWithTag(sealTag)) e.SetActive(on);
    }

    PlayerController* Player() {
        auto players = GetScene().FindAllWithTag("Player");
        return players.empty() ? nullptr : GetScriptOn<PlayerController>(players.front());
    }

    void Begin() {
        m_Active = true;
        m_Wave = -1;
        m_NextWave = 1.0f;
        m_Elapsed = 0.0f;
        m_BestRank = -1;
        if (auto* p = Player()) m_HealthAtStart = p->health;
        m_DamageTaken = 0.0f;
        SetSeals(true);
        m_BannerText = title;
        m_Banner = 2.4f;
        CameraShake::Add(0.3f);
        Audio::PlayOneShot("Audio/seal.wav", 0.8f);
        SoundParams sp;
        sp.loop = true;
        sp.volume = 0.0f;
        sp.bus = AudioBus::Music;
        sp.stream = true;
        m_Music = Audio::Play(music, sp);
        m_MusicVolume = 0.0f;
        if (auto* pc = PlayerCombat::Local()) pc->Style().Reset();
    }

    void SpawnWave(int wave) {
        m_Wave = wave;
        auto regular = Counts(waves), heavy = Counts(heavies);
        int n = wave < (int)regular.size() ? regular[wave] : 0;
        int h = wave < (int)heavy.size() ? heavy[wave] : 0;
        auto points = GetScene().FindAllWithTag(spawnTag);
        if (points.empty()) return;
        int k = wave * 3;   // rotate through the spawn points
        auto spawn = [&](uint64_t templ) {
            Entity t = FindByUUID(templ);
            if (!t) return;
            vec3 at = GetScene().WorldPosition(points[k++ % points.size()]);
            Entity e = Instantiate(t, at);
            e.SetActive(true);
            m_Spawned.push_back(e.GetUUID());
            combat::Burst(GetScene(), at + vec3(0, 0.3f, 0), { 0.8f, 0.05f, 0.15f, 1.0f }, 36, 3.0f, 0.35f, 0.9f);
        };
        for (int i = 0; i < n; i++) spawn(enemyTemplate);
        for (int i = 0; i < h; i++) spawn(heavyTemplate ? heavyTemplate : enemyTemplate);
        Audio::PlayOneShot("Audio/enemy_spawn.wav", 0.7f);
        if (wave > 0) { m_BannerText = "WAVE " + std::to_string(wave + 1); m_Banner = 1.6f; }
    }

    void Finish() {
        m_Active = false;
        m_Done = true;
        SaveGame::SetBool(Key(), true);
        SetSeals(false);
        if (auto* p = Player()) m_DamageTaken = std::max(0.0f, m_HealthAtStart - p->health);
        // Grade: the best style rank, nudged by speed and by not getting hit
        int score = std::max(m_BestRank, 0);
        if (m_DamageTaken <= 0.0f) score += 1;
        if (m_Elapsed < 60.0f) score += 1;
        if (m_DamageTaken > 50.0f) score -= 1;
        m_GradeRank = glm::clamp(score, 0, combat::StyleMeter::kRanks - 1);
        m_Grade = combat::StyleMeter::Letter(m_GradeRank);
        m_Result = 7.0f;
        Audio::PlayOneShot("Audio/area_clear.wav", 0.8f);
        Emit("AreaCleared", { { "rank", m_Grade } });
    }

    bool m_Active = false, m_Done = false;
    int m_Wave = -1;
    float m_NextWave = 0.0f;
    float m_Elapsed = 0.0f;
    float m_Banner = 0.0f, m_Result = 0.0f;
    std::string m_BannerText;
    std::vector<uint64_t> m_Spawned;
    int m_BestRank = -1;
    float m_HealthAtStart = 100.0f, m_DamageTaken = 0.0f;
    std::string m_Grade;
    int m_GradeRank = 0;
    uint32_t m_Music = 0;
    float m_MusicVolume = 0.0f;
};
EU_SCRIPT(BattleZone)
