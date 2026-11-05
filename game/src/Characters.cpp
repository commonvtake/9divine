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

namespace {
Entity Part(Scene& s, Entity parent, const std::string& name, const char* mesh, const vec3& pos, const vec3& scale, const vec4& color,
            float metallic = 0.0f, float roughness = 0.6f) {
    Entity e = s.CreateEntity(name);
    e.Transform().parent = parent.GetUUID();
    e.Transform().position = pos;
    e.Transform().scale = scale;
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = mesh;
    mr.color = color;
    mr.metallic = metallic;
    mr.roughness = roughness;
    return e;
}

ScriptInstance Script(const std::string& name, json fields) {
    ScriptInstance si;
    si.className = name;
    si.fields = std::move(fields);
    return si;
}
} // namespace

Sword AddSword(Scene& s, Entity model, const vec4& blade, const vec4& hilt, bool trail) {
    // Blade along the sword's +Z; the origin is the middle of the grip.
    Sword r;
    r.root = s.CreateEntity("Sword");
    r.root.Transform().parent = model.GetUUID();
    r.root.Add<ScriptComponent>().scripts.push_back(
        Script("AttachToBone", { { "Bone", "Hand.R" }, { "Offset", vec3(0.0f, -0.07f, 0.02f) }, { "Rotation", vec3(0.0f) } }));
    Part(s, r.root, "Grip", "builtin:cube", { 0, 0, 0 }, { 0.04f, 0.04f, 0.22f }, hilt);
    Part(s, r.root, "Guard", "builtin:cube", { 0, 0, 0.13f }, { 0.26f, 0.05f, 0.06f }, hilt, 0.8f, 0.35f);
    Part(s, r.root, "Pommel", "builtin:sphere", { 0, 0, -0.13f }, vec3(0.075f), hilt, 0.8f, 0.35f);
    Entity b = Part(s, r.root, "Blade", "builtin:cube", { 0, 0, 0.66f }, { 0.085f, 0.018f, 1.0f }, blade, 1.0f, 0.18f);
    b.Get<MeshRendererComponent>().castShadows = true;
    Part(s, r.root, "Blade Tip", "builtin:cube", { 0, 0, 1.17f }, { 0.06f, 0.016f, 0.06f }, blade, 1.0f, 0.18f).Transform().SetEulerDegrees({ 0, 45, 0 });
    if (trail) {
        r.trail = s.CreateEntity("Sword Trail");
        r.trail.Transform().parent = r.root.GetUUID();
        r.trail.Transform().position = { 0, 0, 0.95f };
        auto& pe = r.trail.Add<ParticleEmitterComponent>();
        pe.playing = false;
        pe.playOnStart = false;
        pe.rate = 320.0f;
        pe.lifetime = 0.16f;
        pe.lifetimeVariance = 0.04f;
        pe.shapeSize = { 0.03f, 0.03f, 0.25f };
        pe.shape = ParticleShape::Box;
        pe.velocity = vec3(0.0f);
        pe.spread = 0.05f;
        pe.startColor = { 0.75f, 0.9f, 1.0f, 0.9f };
        pe.endColor = { 0.45f, 0.15f, 1.0f, 0.0f };
        pe.startSize = 0.17f;
        pe.endSize = 0.03f;
        pe.additive = true;
        pe.worldSpace = true;
    }
    return r;
}

Entity AddPuppet(Scene& s, const vec3& position, bool brute) {
    Entity e = s.CreateEntity(brute ? "Brute" : "Puppet");
    e.Get<TagComponent>().tag = "Enemy";
    e.Transform().position = position;
    if (brute) e.Transform().scale = vec3(1.3f);
    auto& rb = e.Add<RigidBodyComponent>();
    rb.mass = brute ? 3.0f : 1.0f;
    rb.friction = 0.0f;
    rb.restitution = 0.0f;
    rb.drag = 0.0f;
    auto& col = e.Add<SphereColliderComponent>();
    col.radius = 0.45f;
    col.offset = { 0, 0.45f, 0 };

    Entity model = AddHeroModel(s, e, "PuppetModel");
    auto& mr = model.Get<MeshRendererComponent>();
    ApplyToonPreset(mr, ToonPreset::Killer7);
    mr.color = brute ? vec4(0.55f, 0.22f, 0.2f, 1.0f) : vec4(0.36f, 0.34f, 0.46f, 1.0f);

    // Eyes: two slits that follow the head
    Entity eyes = s.CreateEntity("Eyes");
    eyes.Transform().parent = model.GetUUID();
    eyes.Add<ScriptComponent>().scripts.push_back(Script("AttachToBone", { { "Bone", "Head" }, { "Offset", vec3(0.0f, 0.11f, 0.1f) } }));
    for (int k = -1; k <= 1; k += 2) {
        Entity eye = Part(s, eyes, "Eye", "builtin:cube", { k * 0.045f, 0, 0 }, { 0.04f, 0.012f, 0.012f }, { 1, 0.2f, 0.1f, 1 });
        eye.Get<MeshRendererComponent>().emissive = { 2.5f, 0.3f, 0.12f };
        eye.Get<MeshRendererComponent>().castShadows = false;
    }
    AddSword(s, model, brute ? vec4(0.35f, 0.3f, 0.28f, 1) : vec4(0.45f, 0.42f, 0.4f, 1), { 0.12f, 0.08f, 0.06f, 1 }, false);

    e.Add<ScriptComponent>().scripts.push_back(Script("Puppet", {
        { "Model", (uint64_t)model.GetUUID() }, { "Eyes", (uint64_t)eyes.GetUUID() },
        { "Max Health", brute ? 260.0f : 90.0f }, { "Move Speed", brute ? 2.4f : 3.2f },
        { "Damage", brute ? 22.0f : 12.0f }, { "Heavy", brute }, { "Poise", 45.0f } }));
    return e;
}

} // namespace characters
