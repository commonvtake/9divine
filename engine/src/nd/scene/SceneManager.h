#pragma once
#include "nd/scene/Scene.h"
#include <functional>

namespace nd {

// Owns the active scene and performs scene switches at a safe point in the frame.
// Gameplay code calls SceneManager::LoadScene("Scenes/Level2.scene").
class SceneManager {
public:
    static Ref<Scene> Active() { return s_Active; }
    static void SetActive(Ref<Scene> scene) { s_Active = std::move(scene); }

    // Deferred: the switch happens on the next Update(). If the current scene is running,
    // the new one is started automatically.
    static void LoadScene(const std::string& path, std::function<void(Scene&)> onLoaded = nullptr);
    static void ReloadActive(std::function<void(Scene&)> onLoaded = nullptr);
    static bool HasPendingLoad() { return !s_Pending.empty(); }

    // Returns true if the active scene changed this frame.
    static bool Update();
    // Loads synchronously (used at startup / by the editor).
    static Ref<Scene> LoadNow(const std::string& path);

    // "Quit" from gameplay code: the player app closes, the editor leaves play mode.
    static void RequestQuit() { if (onQuitRequested) onQuitRequested(); }
    static std::function<void()> onQuitRequested;

private:
    static Ref<Scene> s_Active;
    static std::string s_Pending;
    static std::function<void(Scene&)> s_OnLoaded;
};

} // namespace nd
