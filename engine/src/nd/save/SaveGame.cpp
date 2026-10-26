#include "nd/save/SaveGame.h"
#include "nd/scene/Entity.h"
#include "nd/scene/ScriptableEntity.h"
#include "nd/scene/SceneManager.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"
#include <ctime>
#include <set>

namespace nd {

json SaveGame::s_Data = json::object();
bool SaveGame::s_Restoring = false;

fs::path SaveGame::Directory() {
    std::string app = Project::IsLoaded() ? Project::Info().name : "9divine";
    return Platform::UserDataDir(app) / "saves";
}

static fs::path SlotPath(int slot) {
    return SaveGame::Directory() / (slot == SaveGame::kCheckpointSlot ? std::string("checkpoint.json") : "slot_" + std::to_string(slot) + ".json");
}

bool SaveGame::Save(int slot) {
    std::time_t now = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    s_Data["timestamp"] = buf;
    bool ok = FileSystem::WriteText(SlotPath(slot), s_Data.dump(2));
    if (ok) ND_INFO("Game saved to slot {}", slot);
    else ND_ERROR("Could not write save slot {}", slot);
    return ok;
}

bool SaveGame::Load(int slot) {
    auto text = FileSystem::ReadText(SlotPath(slot));
    if (!text) return false;
    try {
        s_Data = json::parse(*text);
    } catch (...) {
        ND_ERROR("Save slot {} is corrupted", slot);
        return false;
    }
    return true;
}

bool SaveGame::SlotExists(int slot) { return fs::exists(SlotPath(slot)); }
void SaveGame::DeleteSlot(int slot) { std::error_code ec; fs::remove(SlotPath(slot), ec); }

std::string SaveGame::SlotTimestamp(int slot) {
    auto text = FileSystem::ReadText(SlotPath(slot));
    if (!text) return "";
    try { return JGet<std::string>(json::parse(*text), "timestamp", ""); } catch (...) { return ""; }
}

void SaveGame::CaptureScene(Scene& scene) {
    json snap;
    snap["scene"] = scene.path;
    snap["entities"] = json::object();
    scene.Reg().View<SaveableComponent>([&](EntityId id, SaveableComponent& s) {
        Entity e(id, &scene);
        json je;
        if (s.saveTransform) {
            auto& t = e.Transform();
            je["position"] = t.position;
            je["rotation"] = t.rotation;
            je["scale"] = t.scale;
        }
        if (s.saveActive) je["active"] = e.Get<TagComponent>().active;
        if (s.saveScripts) {
            if (auto* sc = e.TryGet<ScriptComponent>()) {
                json scripts = json::object();
                for (auto& si : sc->scripts) scripts[si.className] = si.instance ? SaveScriptFields(*si.instance) : si.fields;
                je["scripts"] = scripts;
            }
        }
        snap["entities"][std::to_string((uint64_t)e.GetUUID())] = je;
    });
    s_Data["snapshot"] = snap;
}

void SaveGame::RestoreScene(Scene& scene) {
    if (!s_Data.contains("snapshot")) return;
    const json& snap = s_Data["snapshot"];
    const json& ents = snap["entities"];
    std::vector<Entity> toDestroy;
    scene.Reg().View<SaveableComponent>([&](EntityId id, SaveableComponent& s) {
        Entity e(id, &scene);
        std::string key = std::to_string((uint64_t)e.GetUUID());
        if (!ents.contains(key)) { toDestroy.push_back(e); return; }   // gone when saved (e.g. collected)
        const json& je = ents[key];
        if (s.saveTransform && je.contains("position")) {
            auto& t = e.Transform();
            t.position = JGet(je, "position", t.position);
            t.rotation = JGet(je, "rotation", t.rotation);
            t.scale = JGet(je, "scale", t.scale);
            if (auto* rb = e.TryGet<RigidBodyComponent>()) rb->velocity = vec3(0.0f);
        }
        if (s.saveActive && je.contains("active")) e.Get<TagComponent>().active = je["active"].get<bool>();
        if (s.saveScripts && je.contains("scripts")) {
            if (auto* sc = e.TryGet<ScriptComponent>()) {
                for (auto& si : sc->scripts) {
                    if (!je["scripts"].contains(si.className)) continue;
                    const json& f = je["scripts"][si.className];
                    if (si.instance) LoadScriptFields(*si.instance, f);
                    else si.fields = f;
                }
            }
        }
    });
    for (auto& e : toDestroy) scene.DestroyEntity(e);
}

bool SaveGame::Checkpoint(Scene& scene) {
    CaptureScene(scene);
    return Save(kCheckpointSlot);
}

bool SaveGame::LoadCheckpoint() { return LoadAndRestore(kCheckpointSlot); }

bool SaveGame::LoadAndRestore(int slot) {
    if (!Load(slot)) return false;
    std::string path = s_Data.contains("snapshot") ? JGet<std::string>(s_Data["snapshot"], "scene", "") : "";
    if (path.empty()) return true;
    // Reload the scene fresh, then apply the snapshot once it's running.
    s_Restoring = true;
    SceneManager::LoadScene(path, [](Scene& s) { RestoreScene(s); s_Restoring = false; });
    return true;
}

} // namespace nd
