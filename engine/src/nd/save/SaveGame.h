#pragma once
#include "nd/core/Base.h"
#include "nd/core/Json.h"
#include <filesystem>

namespace nd {

class Scene;

// Player progress: a key/value store saved to numbered slots (JSON files in the user's
// app-data folder), plus scene snapshots for checkpoints. Entities with a Saveable
// component get their transform, active state and script fields captured.
//
//   SaveGame::SetInt("coins", coins);
//   SaveGame::Checkpoint(GetScene());          // writes the checkpoint slot
//   SaveGame::LoadCheckpoint();                // reloads the scene and restores it
class SaveGame {
public:
    static constexpr int kCheckpointSlot = 99;

    static void SetInt(const std::string& key, int v) { s_Data["values"][key] = v; }
    static void SetFloat(const std::string& key, float v) { s_Data["values"][key] = v; }
    static void SetBool(const std::string& key, bool v) { s_Data["values"][key] = v; }
    static void SetString(const std::string& key, const std::string& v) { s_Data["values"][key] = v; }
    static void SetVec3(const std::string& key, const vec3& v) { s_Data["values"][key] = v; }
    static int GetInt(const std::string& key, int fallback = 0) { return JGet(Values(), key.c_str(), fallback); }
    static float GetFloat(const std::string& key, float fallback = 0.0f) { return JGet(Values(), key.c_str(), fallback); }
    static bool GetBool(const std::string& key, bool fallback = false) { return JGet(Values(), key.c_str(), fallback); }
    static std::string GetString(const std::string& key, const std::string& fallback = "") { return JGet(Values(), key.c_str(), fallback); }
    static vec3 GetVec3(const std::string& key, const vec3& fallback = vec3(0.0f)) { return JGet(Values(), key.c_str(), fallback); }
    static bool HasKey(const std::string& key) { return Values().contains(key); }
    static void DeleteKey(const std::string& key) { if (s_Data.contains("values")) s_Data["values"].erase(key); }
    static void Clear() { s_Data = json::object(); }

    static bool Save(int slot = 0);
    static bool Load(int slot = 0);                 // values only
    static bool SlotExists(int slot);
    static void DeleteSlot(int slot);
    static std::string SlotTimestamp(int slot);

    // Snapshot of every Saveable entity in the scene (stored with the values).
    static void CaptureScene(Scene& scene);
    static void RestoreScene(Scene& scene);

    // Capture + save to the checkpoint slot / load it (switches scene if needed).
    static bool Checkpoint(Scene& scene);
    static bool LoadCheckpoint();
    // Loads a slot and restores its scene snapshot (switching scenes if needed).
    static bool LoadAndRestore(int slot);

    static std::filesystem::path Directory();
    // True while a save is being loaded (scene reload in progress) - lets scripts skip
    // "fresh start" logic such as clearing old checkpoints.
    static bool IsRestoring() { return s_Restoring; }

private:
    static const json& Values() { static const json empty = json::object(); return s_Data.contains("values") ? s_Data["values"] : empty; }
    static json s_Data;
    static bool s_Restoring;
};

} // namespace nd
