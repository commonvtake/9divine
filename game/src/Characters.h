#pragma once
#include <euphoria/Euphoria.h>

// Ready-made character models for the demo and project templates.
namespace characters {

// The Hero: a low-poly human made with tools/blender/euphoria_hero.py (Idle/Walk/Run/Jump plus
// sword combat clips).
constexpr const char* kHeroModel = "Models/Hero/Hero.glb";
constexpr float kHeroWalkSpeed = 2.0f;   // m/s the Walk clip is animated for
constexpr float kHeroRunSpeed = 6.0f;    // m/s the Run clip is animated for

// Adds the Hero as a child of `parent` (the entity with the rigid body and PlayerController),
// facing the parent's forward (-Z), toon-shaded, with a locomotion animator ready to drive.
eu::Entity AddHeroModel(eu::Scene& scene, eu::Entity parent, const std::string& name = "PlayerModel");

// A one-handed sword held in the model's right hand (an AttachToBone script follows the
// Hand.R bone). With a trail, a particle emitter at the blade tip leaves a streak while
// PlayerCombat has it switched on.
struct Sword { eu::Entity root, trail; };
Sword AddSword(eu::Scene& scene, eu::Entity model, const eu::vec4& blade, const eu::vec4& hilt, bool trail);

// A Puppet enemy (or a bigger, armoured Brute): the Hero mesh in black ink with glowing eyes
// and a blade. Make it inactive to use it as a template that a BattleZone copies.
eu::Entity AddPuppet(eu::Scene& scene, const eu::vec3& position, bool brute);

} // namespace characters
