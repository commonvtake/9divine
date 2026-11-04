#include "Characters.h"

using namespace eu;

namespace characters {

Entity AddHeroModel(Scene& scene, Entity parent, const std::string& name) {
    Entity m = scene.CreateEntity(name);
    m.Transform().parent = parent.GetUUID();
    // Blender's glTF exporter turns the front view (-Y) into +Z; the engine's forward is -Z.
    m.Transform().SetEulerDegrees({ 0, 180, 0 });
    auto& mr = m.Add<MeshRendererComponent>();
    mr.mesh = kHeroModel;
    ApplyToonPreset(mr, ToonPreset::NoMoreHeroes);
    SetupLocomotion(m.Add<AnimatorComponent>(), { "Idle", "Walk", "Run", "Jump" }, kHeroWalkSpeed, kHeroRunSpeed);
    return m;
}

} // namespace characters
