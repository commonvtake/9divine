#pragma once
#include <euphoria/Euphoria.h>

// Ready-made character models for the demo and project templates.
namespace characters {

// The Hero: a low-poly human made with tools/blender/euphoria_hero.py (Idle/Walk/Run/Jump).
constexpr const char* kHeroModel = "Models/Hero/Hero.glb";
constexpr float kHeroWalkSpeed = 2.0f;   // m/s the Walk clip is animated for
constexpr float kHeroRunSpeed = 6.0f;    // m/s the Run clip is animated for

// Adds the Hero as a child of `parent` (the entity with the rigid body and PlayerController),
// facing the parent's forward (-Z), toon-shaded, with a locomotion animator ready to drive.
eu::Entity AddHeroModel(eu::Scene& scene, eu::Entity parent, const std::string& name = "PlayerModel");

} // namespace characters
