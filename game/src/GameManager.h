#pragma once
#include <nd/9divine.h>

// Owns the game's rules and HUD: ember counting, pause menu, save/load, multiplayer menu.
class GameManager : public nd::ScriptableEntity {
public:
    std::string title = "EMBER ISLE";
    uint64_t player = 0;
    uint64_t portal = 0;
    bool showControls = true;

    static GameManager* Instance() { return s_Instance; }

    void Reflect(nd::FieldVisitor& v) override;
    void OnCreate() override;
    void OnStart() override;
    void OnDestroy() override;
    void OnUpdate(float dt) override;
    void OnGUI() override;
    void OnUIButton(const std::string& name) override;
    void OnNetworkMessage(const std::string& channel, const nd::json& data, uint32_t sender) override;

    void CollectEmber();
    void Notify(const std::string& text, float seconds = 3.0f);
    bool IsPaused() const { return m_Paused; }
    void SetPaused(bool p);
    int Collected() const { return m_Collected; }

private:
    void DrawPauseMenu();
    static GameManager* s_Instance;
    int m_Total = 0;
    int m_Collected = 0;
    bool m_Paused = false;
    bool m_Won = false;
    std::string m_Notice;
    float m_NoticeTime = 0.0f;
    float m_Time = 0.0f;
    std::string m_JoinAddress = "127.0.0.1";
};
