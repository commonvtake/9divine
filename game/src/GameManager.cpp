#include "GameManager.h"
#include "PlayerController.h"

using namespace nd;

GameManager* GameManager::s_Instance = nullptr;

void GameManager::Reflect(FieldVisitor& v) {
    v.String("Title", title);
    v.EntityRef("Player", player);
    v.EntityRef("Portal", portal);
    v.Bool("Show Controls", showControls);
    v.Int("Collected", m_Collected);
}

void GameManager::OnCreate() {
    s_Instance = this;
    if (!SaveGame::IsRestoring()) SaveGame::DeleteSlot(SaveGame::kCheckpointSlot);   // fresh run
}

void GameManager::OnStart() {
    int remaining = 0;
    for (auto& e : GetScene().FindAllWithTag("Ember")) if (e.IsActive()) remaining++;
    m_Total = remaining + m_Collected;
    if (Entity p = FindByUUID(portal)) if (m_Total > 0) p.SetActive(m_Collected >= m_Total);
    Input::SetCursorMode(CursorMode::Locked);
    if (m_Total > 0 && m_Collected == 0) Notify("Find every ember on the island", 4.0f);
}

void GameManager::OnDestroy() {
    if (s_Instance == this) s_Instance = nullptr;
    Input::SetCursorMode(CursorMode::Normal);
    Time::SetScale(1.0f);
}

void GameManager::SetPaused(bool p) {
    m_Paused = p;
    Time::SetScale(p ? 0.0f : 1.0f);
    Input::SetCursorMode(p ? CursorMode::Normal : CursorMode::Locked);
}

void GameManager::OnUpdate(float dt) {
    m_Time += Time::UnscaledDelta();
    m_NoticeTime -= Time::UnscaledDelta();
    if (Input::GetActionPressed("Pause")) SetPaused(!m_Paused);
    if (Input::IsKeyPressed(Key::F1)) showControls = !showControls;
    // The controls card is made of UIElement entities (retained UI) - toggle them together.
    for (auto& e : GetScene().FindAllWithTag("ControlsUI"))
        if (auto* ui = e.TryGet<UIElementComponent>()) ui->visible = showControls && !m_Paused;
    // Clicking back into a running game re-captures the mouse.
    if (!m_Paused && Input::GetCursorMode() != CursorMode::Locked && Input::IsMousePressed(Mouse::Left)) Input::SetCursorMode(CursorMode::Locked);
}

void GameManager::CollectEmber() {
    m_Collected++;
    ND_INFO("Ember collected ({}/{})", m_Collected, m_Total);
    Audio::PlayOneShot("Audio/coin.wav", 0.7f);
    if (Network::IsActive()) Network::Send("ember", { { "count", m_Collected } });
    if (m_Collected >= m_Total && !m_Won) {
        m_Won = true;
        Notify("All embers found! The portal has opened.", 5.0f);
        if (Entity p = FindByUUID(portal)) p.SetActive(true);
    } else {
        Notify("Ember " + std::to_string(m_Collected) + " / " + std::to_string(m_Total), 2.0f);
    }
}

void GameManager::Notify(const std::string& text, float seconds) {
    m_Notice = text;
    m_NoticeTime = seconds;
}

void GameManager::OnNetworkMessage(const std::string& channel, const json& data, uint32_t sender) {
    if (channel == "ember") Notify("Player " + std::to_string(sender) + " found an ember!", 2.5f);
    if (channel == "chat") Notify("Player " + std::to_string(sender) + ": " + JGet<std::string>(data, "text", ""), 3.0f);
}

void GameManager::OnUIButton(const std::string& name) {
    if (name == "ControlsCloseButton") showControls = false;
}

void GameManager::OnGUI() {
    vec2 canvas = UI::CanvasSize();
    PlayerController* pc = GetScriptOn<PlayerController>(FindByUUID(player));

    // Health
    if (pc) {
        UI::PanelBordered(UI::Anchored(UIAnchor::TopLeft, { 28, 28 }, { 420, 92 }), { 0, 0, 0, 0.45f }, { 1, 1, 1, 0.12f });
        UI::Text({ 48, 38 }, "HEALTH", 22, { 1, 1, 1, 0.7f });
        UI::ProgressBar(UI::Anchored(UIAnchor::TopLeft, { 48, 72 }, { 380, 26 }), pc->health / pc->maxHealth, { 0.86f, 0.28f, 0.25f, 1 });
        if (pc->HurtFlash() > 0.0f) UI::Panel({ { 0, 0 }, canvas }, { 0.8f, 0.0f, 0.0f, pc->HurtFlash() * 0.25f });
    }
    // Embers (only in scenes that have them)
    if (m_Total > 0) {
    UI::PanelBordered(UI::Anchored(UIAnchor::TopRight, { 28, 28 }, { 260, 92 }), { 0, 0, 0, 0.45f }, { 1, 1, 1, 0.12f });
    UI::Text({ canvas.x - 268, 38 }, "EMBERS", 22, { 1, 1, 1, 0.7f });
    UI::Text({ canvas.x - 268, 64 }, std::to_string(m_Collected) + " / " + std::to_string(m_Total), 40, { 1.0f, 0.78f, 0.35f, 1 });
    }

    // Network status
    if (Network::IsActive()) UI::Text({ canvas.x - 48, canvas.y - 52 }, Network::Status(), 22, { 0.6f, 1, 0.85f, 0.9f }, UI::Align::Right);

    // Title card for the first seconds
    if (m_Time < 4.0f) {
        float a = m_Time < 3.0f ? 1.0f : 4.0f - m_Time;
        UI::Text({ canvas.x * 0.5f + 3, 173 }, title, 84, { 0, 0, 0, a * 0.45f }, UI::Align::Center);
        UI::Text({ canvas.x * 0.5f, 170 }, title, 84, { 1, 1, 1, a }, UI::Align::Center);
    }
    // Notifications
    if (m_NoticeTime > 0.0f) {
        float a = std::min(1.0f, m_NoticeTime);
        float w = UI::MeasureText(m_Notice, 34) + 60;
        UI::Panel({ { (canvas.x - w) * 0.5f, canvas.y - 190 }, { w, 60 } }, { 0, 0, 0, 0.55f * a });
        UI::Text({ canvas.x * 0.5f, canvas.y - 178 }, m_Notice, 34, { 1, 1, 1, a }, UI::Align::Center);
    }
    // Crosshair dot
    if (!m_Paused) UI::Panel({ canvas * 0.5f - vec2(3), { 6, 6 } }, { 1, 1, 1, 0.6f });

    if (m_Paused) DrawPauseMenu();
}

void GameManager::DrawPauseMenu() {
    vec2 c = UI::CanvasSize();
    UI::Panel({ { 0, 0 }, c }, { 0.02f, 0.04f, 0.05f, 0.6f });
    UI::Text({ c.x * 0.5f, 210 }, "PAUSED", 72, { 1, 1, 1, 1 }, UI::Align::Center);
    float y = -170;
    auto button = [&](const std::string& label) {
        bool clicked = UI::Button(label, UI::Anchored(UIAnchor::Center, { 0, y }, { 420, 64 }));
        y += 78;
        return clicked;
    };
    if (button("Resume")) SetPaused(false);
    if (button("Save Game")) {
        SaveGame::SetInt("embers", m_Collected);
        SaveGame::CaptureScene(GetScene());
        SaveGame::Save(0);
        Notify("Game saved", 2.0f);
    }
    if (button(SaveGame::SlotExists(0) ? "Load Game  (" + SaveGame::SlotTimestamp(0).substr(11) + ")" : "Load Game")) {
        if (SaveGame::LoadAndRestore(0)) { SetPaused(false); return; }
        Notify("No saved game yet", 2.0f);
    }
    if (!Network::IsActive()) {
        if (button("Host Multiplayer")) { Network::Host(7777); SetPaused(false); }
        if (button("Join " + m_JoinAddress)) { Network::Connect(m_JoinAddress, 7777); SetPaused(false); }
    } else {
        if (button("Leave Multiplayer")) Network::Disconnect();
    }
    if (button("Quit")) { SetPaused(false); SceneManager::RequestQuit(); }
}

ND_SCRIPT(GameManager)
