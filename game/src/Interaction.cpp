#include "Interaction.h"
#include "GameManager.h"

using namespace eu;

namespace {
void Say(const std::string& text, float seconds) {
    if (GameManager* gm = GameManager::Instance()) gm->Notify(text, seconds);
    else EU_INFO("{}", text);
}
} // namespace

// ------------------------------------------------------------------ Inventory
namespace Inventory {
bool Has(const std::string& item) { return SaveGame::GetBool("item:" + item); }
void Give(const std::string& item) { SaveGame::SetBool("item:" + item, true); }
}

// ------------------------------------------------------------------ Interactable
void Interactable::Reflect(FieldVisitor& v) {
    v.String("Label", label);
    v.Float("Range", range);
    v.Vec3("Offset", offset);
}

vec3 Interactable::Point() {
    mat4 w = GetScene().WorldMatrix(GetEntity());
    // Offset in the entity's rotated frame, ignoring scale (a door slab is scaled 0.1 thick)
    mat3 r(glm::normalize(vec3(w[0])), glm::normalize(vec3(w[1])), glm::normalize(vec3(w[2])));
    return vec3(w[3]) + r * offset;
}

void Interactable::OnMessage(const std::string& message) {
    if (message != "Interact" || !CanInteract()) return;
    auto players = GetScene().FindAllWithTag("Player");
    Interact(players.empty() ? Entity() : players.front());
}

Interactable* Interactable::FindBest(Scene& scene, const vec3& pos, const vec3& forward) {
    vec3 f(forward.x, 0.0f, forward.z);
    f = glm::length(f) > 1e-4f ? glm::normalize(f) : vec3(0, 0, -1);
    Interactable* best = nullptr;
    float bestScore = 1e9f;
    scene.Reg().View<ScriptComponent>([&](EntityId id, ScriptComponent& sc) {
        Entity e(id, &scene);
        if (!e.IsActive()) return;
        for (auto& s : sc.scripts) {
            auto* it = dynamic_cast<Interactable*>(s.instance.get());
            if (!it || !it->CanInteract()) continue;
            vec3 d = it->Point() - pos;
            if (std::abs(d.y) > 2.2f) continue;             // another floor
            d.y = 0.0f;
            float dist = glm::length(d);
            if (dist > it->range) continue;
            float facing = dist > 1e-3f ? glm::dot(d / dist, f) : 1.0f;
            if (facing < 0.2f && dist > 0.8f) continue;     // behind or beside us
            float score = dist - facing * 0.75f;
            if (score < bestScore) { bestScore = score; best = it; }
        }
    });
    return best;
}

// ------------------------------------------------------------------ Door
void Door::Reflect(FieldVisitor& v) {
    Interactable::Reflect(v);
    v.String("Target Scene", targetScene);
    v.String("Spawn Point", spawnPoint);
    v.Bool("Locked", locked);
    v.String("Locked Message", lockedMessage);
    v.String("Key Item", keyItem);
    v.String("Open Sound", openSound);
    v.String("Locked Sound", lockedSound);
    v.Float("Fade Out", fadeOut);
    v.Float("Hold", hold);
    v.Float("Fade In", fadeIn);
}

void Door::OnCreate() {
    if (locked && SaveGame::GetBool(StateKey())) locked = false;   // unlocked earlier this session
}

void Door::Interact(Entity) {
    if (ScreenFade::Busy()) return;
    if (locked) {
        if (!keyItem.empty() && Inventory::Has(keyItem)) {
            // Unlocking is its own action, like the old survival-horror games: press again to go in.
            locked = false;
            SaveGame::SetBool(StateKey(), true);
            Audio::PlayOneShot("Audio/door_unlock.wav", 0.9f);
            Say("You used the " + keyItem + ".\nThe door is now unlocked.", 3.0f);
        } else {
            Audio::PlayOneShot(lockedSound, 0.8f);
            Say(lockedMessage, 3.0f);
        }
        return;
    }
    if (!openSound.empty()) Audio::PlayOneShot(openSound, 0.9f);
    std::string target = targetScene, spawn = spawnPoint;
    Scene* here = &GetScene();
    // Everything after this runs while the screen is fully black: the door entity (and this
    // whole scene) may be gone by then, so capture values, not `this`.
    ScreenFade::Transition(fadeOut, hold, fadeIn, [target, spawn, here] {
        if (target.empty()) {
            if (SceneManager::Active().get() == here) PlaceAtSpawn(*here, spawn);
        } else {
            SceneManager::LoadScene(target, [spawn](Scene& s) { if (!spawn.empty()) PlaceAtSpawn(s, spawn); });
        }
    });
}

bool Door::PlaceAtSpawn(Scene& scene, const std::string& spawnName) {
    Entity spawn = scene.FindByName(spawnName);
    if (!spawn) {
        EU_WARN("Door: spawn point '{}' not found", spawnName);
        return false;
    }
    mat4 w = scene.WorldMatrix(spawn);
    vec3 pos = vec3(w[3]);
    vec3 fwd = vec3(w * vec4(0, 0, -1, 0));
    float yaw = std::atan2(-fwd.x, -fwd.z);   // rotation about +Y whose forward (-Z) is fwd
    bool moved = false;
    for (Entity p : scene.FindAllWithTag("Player")) {
        if (!Network::IsLocal(p)) continue;
        auto& t = p.Transform();
        t.position = pos;
        t.rotation = glm::angleAxis(yaw, vec3(0, 1, 0));
        if (auto* rb = p.TryGet<RigidBodyComponent>()) {
            rb->velocity = vec3(0.0f);
            rb->interpValid = false;   // a teleport, not motion to smooth over
        }
        moved = true;
    }
    // Cameras following the player listen for this to cut instead of swooping across the map.
    scene.Events().Emit("PlayerTeleported", { { "yaw", glm::degrees(yaw) } });
    return moved;
}

// ------------------------------------------------------------------ Examine
void Examine::Reflect(FieldVisitor& v) {
    Interactable::Reflect(v);
    v.String("Message", message);
    v.String("Give Item", giveItem);
    v.String("Taken Message", takenMessage);
    v.Bool("Hide When Taken", hideWhenTaken);
}

void Examine::OnCreate() {
    if (!giveItem.empty() && (SaveGame::GetBool(StateKey()) || Inventory::Has(giveItem))) {
        m_Taken = true;
        if (hideWhenTaken) GetEntity().SetActive(false);
    }
}

void Examine::Interact(Entity) {
    if (!giveItem.empty() && !m_Taken) {
        m_Taken = true;
        Inventory::Give(giveItem);
        SaveGame::SetBool(StateKey(), true);
        Audio::PlayOneShot("Audio/coin.wav", 0.6f);
        Say(message + "\nObtained: " + giveItem, 4.0f);
        if (hideWhenTaken) GetEntity().SetActive(false);
        return;
    }
    Say(m_Taken && !takenMessage.empty() ? takenMessage : message, 3.5f);
}

EU_SCRIPT(Door)
EU_SCRIPT(Examine)
