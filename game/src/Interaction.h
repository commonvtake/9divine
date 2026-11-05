#pragma once
#include <euphoria/Euphoria.h>

// Things the player can use with the Interact action (E / gamepad X): doors, notes, items.
// The player looks for the nearest one in front of it, shows a "[E] Open  Noodle Bar" prompt,
// and calls Interact() when the button is pressed.
//
// Derive from Interactable to make new kinds of usable objects:
//
//   class Lever : public Interactable {
//       std::string Verb() const override { return "Pull"; }
//       void Interact(eu::Entity player) override { Emit("LeverPulled"); }
//   };
//   EU_SCRIPT(Lever)
class Interactable : public eu::ScriptableEntity {
public:
    std::string label;               // shown after the verb: "Open  Noodle Bar"
    float range = 1.8f;              // metres from the interaction point
    eu::vec3 offset{ 0.0f };         // interaction point, in the entity's local space (unscaled)

    void Reflect(eu::FieldVisitor& v) override;

    virtual std::string Verb() const { return "Examine"; }
    virtual bool CanInteract() { return true; }
    virtual void Interact(eu::Entity player) {}
    // Event Triggers and scripts can use it too: Send Message "Interact" (e.g. a cutscene door).
    void OnMessage(const std::string& message) override;

    eu::vec3 Point();

    // The best interactable for a player standing at `pos` facing `forward`, or nullptr.
    // Prefers close objects in front of the player; objects behind it only count when touching.
    static Interactable* FindBest(eu::Scene& scene, const eu::vec3& pos, const eu::vec3& forward);
};

// A door. With a Target Scene it fades to black and loads that scene; without one it moves
// the player within the current scene. Either way the player is placed at the entity named
// Spawn Point (facing the way it faces) and the camera snaps behind them.
//
// Locked doors show their message. A door with a Key Item unlocks once the player holds that
// item (see Examine's Give Item), and stays unlocked for the rest of the session.
class Door : public Interactable {
public:
    std::string targetScene;         // empty = same scene
    std::string spawnPoint;          // entity name in the destination
    bool locked = false;
    std::string lockedMessage = "It's locked.";
    std::string keyItem;             // unlocks the door when held
    std::string openSound = "Audio/door_open.wav";
    std::string lockedSound = "Audio/door_locked.wav";
    float fadeOut = 0.45f, hold = 1.1f, fadeIn = 0.6f;

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    std::string Verb() const override { return "Open"; }
    void Interact(eu::Entity player) override;

    // Moves the local player to the named spawn entity and snaps the camera. Returns false if
    // the scene has no such entity (the player then stays where the scene put them).
    static bool PlaceAtSpawn(eu::Scene& scene, const std::string& spawnName);

private:
    std::string StateKey() { return "door_unlocked:" + std::to_string((uint64_t)GetEntity().GetUUID()); }
};

// Something to look at: shows a message, and optionally hands the player an item (a key).
class Examine : public Interactable {
public:
    std::string message = "Nothing special.";
    std::string giveItem;            // e.g. "Apartment Key"
    std::string takenMessage;        // shown on later looks once the item is taken
    bool hideWhenTaken = false;      // the entity disappears with the item

    void Reflect(eu::FieldVisitor& v) override;
    void OnCreate() override;
    std::string Verb() const override { return giveItem.empty() || m_Taken ? "Examine" : "Take"; }
    void Interact(eu::Entity player) override;

private:
    std::string StateKey() { return "taken:" + std::to_string((uint64_t)GetEntity().GetUUID()); }
    bool m_Taken = false;
};

// The player's items, kept for the whole session (and in save games).
namespace Inventory {
bool Has(const std::string& item);
void Give(const std::string& item);
}
