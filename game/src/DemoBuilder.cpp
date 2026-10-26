#include "DemoBuilder.h"
#include <nd/9divine.h>
#include <nd/scene/SceneSerializer.h>
#include <nd/core/FileSystem.h>
#include <cmath>

using namespace nd;

namespace demo {

namespace {

ScriptInstance Script(const std::string& name, json fields = json::object()) {
    ScriptInstance s;
    s.className = name;
    s.fields = std::move(fields);
    return s;
}

void AddScript(Entity e, const std::string& name, json fields = json::object()) {
    e.GetOrAdd<ScriptComponent>().scripts.push_back(Script(name, std::move(fields)));
}

Entity Box(Scene& s, const std::string& name, const vec3& pos, const vec3& size, const vec4& color, const std::string& texture = "",
           float tiling = 1.0f, BodyType body = BodyType::Static, const vec3& euler = vec3(0.0f)) {
    Entity e = s.CreateEntity(name);
    auto& t = e.Transform();
    t.position = pos;
    t.scale = size;
    t.SetEulerDegrees(euler);
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = "builtin:cube";
    mr.color = color;
    mr.texture = texture;
    mr.tiling = tiling;
    e.Add<BoxColliderComponent>();
    if (body != BodyType::Static) {
        auto& rb = e.Add<RigidBodyComponent>();
        rb.type = body;
        rb.mass = size.x * size.y * size.z * 4.0f;
        rb.friction = 0.7f;
    }
    return e;
}

Entity Mesh(Scene& s, const std::string& name, const std::string& mesh, const vec3& pos, const vec3& scale, const vec4& color, Entity parent = {}) {
    Entity e = s.CreateEntity(name);
    e.Transform().position = pos;
    e.Transform().scale = scale;
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = mesh;
    mr.color = color;
    if (parent) e.Transform().parent = parent.GetUUID();
    return e;
}

void SetupFoxAnimator(AnimatorComponent& a) {
    a.params = { { "Speed", AnimParamType::Float, 0.0f } };
    AnimState loco;
    loco.name = "Locomotion";
    loco.blendParam = "Speed";
    loco.blend = { { "Survey", 0.0f }, { "Walk", 2.5f }, { "Run", 7.0f } };
    a.states = { loco };
    a.defaultState = "Locomotion";
}

Entity FoxModel(Scene& s, Entity parent, const std::string& name) {
    Entity m = s.CreateEntity(name);
    m.Transform().parent = parent.GetUUID();
    m.Transform().scale = vec3(0.012f);
    m.Transform().SetEulerDegrees({ 0, 180, 0 });   // the fox faces +Z; our forward is -Z
    auto& mr = m.Add<MeshRendererComponent>();
    mr.mesh = "Models/Fox/Fox.glb";
    SetupFoxAnimator(m.Add<AnimatorComponent>());
    return m;
}

struct PlayerRig { Entity player, model, camera; };

PlayerRig AddPlayer(Scene& s, const vec3& pos) {
    PlayerRig r;
    r.player = s.CreateEntity("Player");
    r.player.Get<TagComponent>().tag = "Player";
    r.player.Transform().position = pos;
    auto& rb = r.player.Add<RigidBodyComponent>();
    rb.mass = 1.0f;
    rb.friction = 0.0f;
    rb.restitution = 0.0f;
    rb.drag = 0.0f;
    auto& col = r.player.Add<SphereColliderComponent>();
    col.radius = 0.45f;
    col.offset = { 0, 0.45f, 0 };
    r.player.Add<AudioListenerComponent>();
    r.player.Add<SaveableComponent>();
    r.player.Add<NetworkIdentityComponent>();
    r.model = FoxModel(s, r.player, "PlayerModel");

    r.camera = s.CreateEntity("Main Camera");
    r.camera.Transform().position = pos + vec3(0, 3, 6);
    r.camera.Add<CameraComponent>();
    AddScript(r.camera, "ThirdPersonCamera", { { "Target", (uint64_t)r.player.GetUUID() }, { "Distance", 6.5f } });
    AddScript(r.player, "PlayerController", { { "Camera", (uint64_t)r.camera.GetUUID() }, { "Model", (uint64_t)r.model.GetUUID() } });
    return r;
}

Entity AddSun(Scene& s, const vec3& euler, float intensity) {
    Entity sun = s.CreateEntity("Sun");
    sun.Transform().position = { 0, 30, 0 };
    sun.Transform().SetEulerDegrees(euler);
    auto& l = sun.Add<LightComponent>();
    l.type = LightType::Directional;
    l.color = { 1.0f, 0.95f, 0.86f };
    l.intensity = intensity;
    return sun;
}

void AddControlsCard(Scene& s, const std::string& extra) {
    Entity panel = s.CreateEntity("ControlsPanel");
    panel.Get<TagComponent>().tag = "ControlsUI";
    auto& p = panel.Add<UIElementComponent>();
    p.type = UIType::Panel;
    p.anchor = UIAnchor::BottomLeft;
    p.position = { 28, 28 };
    p.size = { 470, 300 };
    p.color = { 0, 0, 0, 0.45f };

    Entity text = s.CreateEntity("ControlsText");
    text.Get<TagComponent>().tag = "ControlsUI";
    auto& t = text.Add<UIElementComponent>();
    t.type = UIType::Text;
    t.anchor = UIAnchor::BottomLeft;
    t.position = { 50, 110 };
    t.size = { 430, 200 };
    t.fontSize = 24;
    t.layer = 1;
    t.text = "WASD / Left stick - move\nMouse / Right stick - camera\nShift - sprint     Space - jump\n" + extra + "Esc - pause menu";

    Entity btn = s.CreateEntity("ControlsCloseButton");
    btn.Get<TagComponent>().tag = "ControlsUI";
    auto& b = btn.Add<UIElementComponent>();
    b.type = UIType::Button;
    b.anchor = UIAnchor::BottomLeft;
    b.position = { 50, 46 };
    b.size = { 200, 46 };
    b.text = "Hide (F1)";
    b.fontSize = 22;
    b.layer = 1;
}

Entity AddAudio(Scene& s, const std::string& name, const std::string& clip, float volume, bool music, bool spatial, Entity parent = {}) {
    Entity e = s.CreateEntity(name);
    if (parent) e.Transform().parent = parent.GetUUID();
    auto& a = e.Add<AudioSourceComponent>();
    a.clip = clip;
    a.volume = volume;
    a.loop = true;
    a.music = music;
    a.spatial = spatial;
    a.maxDistance = 25.0f;
    return e;
}

Entity AddPortal(Scene& s, const vec3& pos, const std::string& target, const vec4& color) {
    Entity portal = s.CreateEntity("Portal");
    portal.Transform().position = pos;
    auto& trig = portal.Add<BoxColliderComponent>();
    trig.size = { 2.5f, 3.0f, 2.5f };
    trig.offset = { 0, 1.8f, 0 };
    trig.isTrigger = true;
    AddScript(portal, "Portal", { { "Target Scene", target } });
    vec3 emissive = vec3(color) * 1.1f;
    for (int i = 0; i < 10; i++) {
        float a = i / 10.0f * 2.0f * PI;
        Entity seg = Mesh(s, "Ring", "builtin:cube", { std::cos(a) * 1.6f, 1.9f + std::sin(a) * 1.6f, 0 }, { 0.35f, 0.35f, 0.35f }, { 0.2f, 0.2f, 0.25f, 1 }, portal);
        seg.Transform().SetEulerDegrees({ 0, 0, glm::degrees(a) });
        seg.Get<MeshRendererComponent>().emissive = emissive * 0.4f;
    }
    Entity disc = Mesh(s, "Gate", "builtin:cylinder", { 0, 1.9f, 0 }, { 2.8f, 0.05f, 2.8f }, color, portal);
    disc.Transform().SetEulerDegrees({ 90, 0, 0 });
    disc.Get<MeshRendererComponent>().emissive = emissive;
    disc.Get<MeshRendererComponent>().castShadows = false;
    Entity fx = s.CreateEntity("PortalFX");
    fx.Transform().parent = portal.GetUUID();
    fx.Transform().position = { 0, 1.9f, 0 };
    auto& pe = fx.Add<ParticleEmitterComponent>();
    ParticlePresets::Magic(pe);
    pe.startColor = color;
    pe.shapeSize = vec3(1.4f);
    Entity light = s.CreateEntity("PortalLight");
    light.Transform().parent = portal.GetUUID();
    light.Transform().position = { 0, 1.9f, 1.0f };
    auto& l = light.Add<LightComponent>();
    l.type = LightType::Point;
    l.color = vec3(color);
    l.intensity = 3.0f;
    l.range = 8.0f;
    return portal;
}

Entity AddGameManager(Scene& s, const std::string& title, Entity player, Entity portal) {
    Entity gm = s.CreateEntity("GameManager");
    gm.Add<SaveableComponent>().saveTransform = false;
    AddScript(gm, "GameManager", { { "Title", title }, { "Player", (uint64_t)player.GetUUID() }, { "Portal", portal ? (uint64_t)portal.GetUUID() : 0 } });
    return gm;
}

// ------------------------------------------------------------------ Island
void BuildIsland(const std::string& path) {
    Scene s;
    s.name = "Ember Isle";
    auto& env = s.Environment();
    env.skyTop = { 0.30f, 0.50f, 0.85f };
    env.skyHorizon = { 0.86f, 0.78f, 0.70f };
    env.fogColor = { 0.80f, 0.76f, 0.72f };
    env.fogDensity = 0.0075f;
    env.ambientIntensity = 0.5f;
    s.Nav().settings.minWalkableHeight = 0.7f;   // keep AI out of the sea
    s.Nav().settings.cellSize = 0.6f;

    AddSun(s, { -38, 40, 0 }, 2.2f);

    Entity terrainE = s.CreateEntity("Terrain");
    auto& terrain = terrainE.Add<TerrainComponent>();
    terrain.seed = 2026;
    terrain.size = 140.0f;
    terrain.heightScale = 16.0f;
    terrain.grassColor = { 0.27f, 0.47f, 0.18f };
    TerrainSystem::Generate(terrain);
    auto ground = [&](float x, float z) { return terrain.HeightAtLocal(x, z); };

    Entity water = s.CreateEntity("Water");
    water.Transform().position = { 0, 0.55f, 0 };
    water.Add<WaterComponent>().size = { 420, 420 };

    PlayerRig rig = AddPlayer(s, { 0, ground(0, 6) + 0.6f, 6 });

    // Embers: placed on a spiral around the island, on dry land.
    uint32_t seed = 99;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    int placed = 0;
    for (int attempt = 0; attempt < 400 && placed < 10; attempt++) {
        float a = attempt * 2.399f;
        float r = 9.0f + (attempt % 37) * 1.25f + rnd() * 4.0f;
        float x = std::cos(a) * r, z = std::sin(a) * r;
        float h = ground(x, z);
        if (h < 1.3f || h > 11.0f || terrain.NormalAtLocal(x, z).y < 0.8f) continue;
        Entity e = Mesh(s, "Ember " + std::to_string(placed + 1), "builtin:cube", { x, h + 1.0f, z }, { 0.38f, 0.6f, 0.38f }, { 1.0f, 0.62f, 0.25f, 1 });
        e.Get<TagComponent>().tag = "Ember";
        e.Transform().SetEulerDegrees({ 45, 0, 45 });
        e.Get<MeshRendererComponent>().emissive = { 2.2f, 0.9f, 0.2f };
        auto& c = e.Add<SphereColliderComponent>();
        c.radius = 1.4f;
        c.isTrigger = true;
        e.Add<SaveableComponent>().saveTransform = false;
        AddScript(e, "Ember");
        Entity fx = s.CreateEntity("EmberGlow");
        fx.Transform().parent = e.GetUUID();
        auto& pe = fx.Add<ParticleEmitterComponent>();
        ParticlePresets::Magic(pe);
        pe.startColor = { 1.0f, 0.75f, 0.3f, 1 };
        pe.endColor = { 1.0f, 0.3f, 0.05f, 0 };
        pe.rate = 18;
        placed++;
    }

    // Campfire checkpoint
    vec3 fire(7.0f, 0, -3.0f);
    fire.y = ground(fire.x, fire.z);
    Entity camp = s.CreateEntity("Campfire");
    camp.Transform().position = fire;
    auto& trig = camp.Add<BoxColliderComponent>();
    trig.size = { 4, 3, 4 };
    trig.offset = { 0, 1, 0 };
    trig.isTrigger = true;
    for (int i = 0; i < 4; i++) {
        Entity log = Mesh(s, "Log", "builtin:cylinder", { 0, 0.15f, 0 }, { 0.22f, 1.3f, 0.22f }, { 0.36f, 0.24f, 0.15f, 1 }, camp);
        log.Transform().SetEulerDegrees({ 75, i * 45.0f, 0 });
    }
    for (int i = 0; i < 8; i++) {
        float a = i / 8.0f * 2 * PI;
        Mesh(s, "Stone", "builtin:sphere", { std::cos(a) * 0.95f, 0.1f, std::sin(a) * 0.95f }, { 0.4f, 0.28f, 0.35f }, { 0.45f, 0.43f, 0.42f, 1 }, camp);
    }
    auto addFx = [&](const char* name, void (*preset)(ParticleEmitterComponent&), float y) {
        Entity fx = s.CreateEntity(name);
        fx.Transform().parent = camp.GetUUID();
        fx.Transform().position = { 0, y, 0 };
        preset(fx.Add<ParticleEmitterComponent>());
    };
    addFx("Fire", ParticlePresets::Fire, 0.3f);
    addFx("Smoke", ParticlePresets::Smoke, 1.3f);
    addFx("Sparks", ParticlePresets::Sparks, 0.5f);
    Entity fireLight = s.CreateEntity("FireLight");
    fireLight.Transform().parent = camp.GetUUID();
    fireLight.Transform().position = { 0, 1.0f, 0 };
    auto& fl = fireLight.Add<LightComponent>();
    fl.type = LightType::Point;
    fl.color = { 1.0f, 0.58f, 0.25f };
    fl.intensity = 3.0f;
    fl.range = 11.0f;
    AddScript(camp, "Campfire", { { "Light", (uint64_t)fireLight.GetUUID() } });
    AddAudio(s, "CampfireSound", "Audio/campfire.wav", 0.8f, false, true, camp);

    // Ruins: stone pillars and an arch (static colliders) + a stack of crates (dynamic)
    vec3 ruins(-12.0f, 0, 2.0f);
    ruins.y = ground(ruins.x, ruins.z);
    const vec4 stone(0.85f, 0.83f, 0.8f, 1);
    for (int i = 0; i < 6; i++) {
        float a = i / 6.0f * 2 * PI;
        float h = i % 3 == 0 ? 1.4f : 3.2f;
        vec3 p = ruins + vec3(std::cos(a) * 4.5f, h * 0.5f - 0.2f, std::sin(a) * 4.5f);
        p.y = ground(p.x, p.z) + h * 0.5f - 0.2f;
        Box(s, "Pillar", p, { 0.9f, h, 0.9f }, stone, "Textures/stone.png", 1.0f);
    }
    {
        // Lintel resting on the two tall pillars at 60 and 120 degrees
        float z = std::sin(PI / 3.0f) * 4.5f;
        float top = std::max(ground(ruins.x + 2.25f, ruins.z + z), ground(ruins.x - 2.25f, ruins.z + z)) + 3.0f;
        Box(s, "Lintel", vec3(ruins.x, top + 0.3f, ruins.z + z), { 5.6f, 0.6f, 1.1f }, stone, "Textures/stone.png", 1.0f);
    }
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3 - y; x++) {
            vec3 p = ruins + vec3(-1.0f + x * 1.05f + y * 0.52f, 0.0f, 0.5f);
            p.y = ground(p.x, p.z) + 0.5f + y * 1.02f + 0.02f;
            Box(s, "Crate", p, vec3(1.0f), vec4(1.0f), "Textures/crate.png", 1.0f, BodyType::Dynamic);
        }

    // Enemies
    vec3 shadeSpots[] = { { 26, 0, -14 }, { -24, 0, -22 }, { 18, 0, 26 } };
    int si = 0;
    for (auto p : shadeSpots) {
        p.y = ground(p.x, p.z) + 0.55f;
        Entity shade = Mesh(s, "Shade " + std::to_string(++si), "builtin:sphere", p, vec3(1.1f), { 0.16f, 0.12f, 0.22f, 1 });
        shade.Get<TagComponent>().tag = "Enemy";
        shade.Get<MeshRendererComponent>().emissive = { 0.08f, 0.0f, 0.12f };
        shade.Get<MeshRendererComponent>().specular = 0.8f;
        shade.Get<MeshRendererComponent>().shininess = 64.0f;
        shade.Add<RigidBodyComponent>().type = BodyType::Kinematic;
        shade.Add<SphereColliderComponent>().radius = 0.5f;
        auto& agent = shade.Add<NavAgentComponent>();
        agent.baseOffset = 0.55f;
        AddScript(shade, "EnemyAI", { { "Player", (uint64_t)rig.player.GetUUID() } });
        for (int k = -1; k <= 1; k += 2) {
            Entity eye = Mesh(s, "Eye", "builtin:sphere", { k * 0.17f, 0.12f, -0.42f }, vec3(0.12f), { 1, 0.2f, 0.15f, 1 }, shade);
            eye.Get<MeshRendererComponent>().emissive = { 3.0f, 0.4f, 0.2f };
        }
        shade.Add<SaveableComponent>();
    }

    // Wildlife
    vec3 foxSpots[] = { { -6, 0, 16 }, { 14, 0, 10 }, { -16, 0, -10 } };
    int fi = 0;
    for (auto p : foxSpots) {
        p.y = ground(p.x, p.z);
        Entity fox = s.CreateEntity("Fox " + std::to_string(++fi));
        fox.Transform().position = p;
        auto& agent = fox.Add<NavAgentComponent>();
        agent.speed = 2.0f;
        Entity model = FoxModel(s, fox, "FoxModel");
        model.Transform().scale = vec3(0.009f);
        AddScript(fox, "FoxWanderer", { { "Player", (uint64_t)rig.player.GetUUID() }, { "Model", (uint64_t)model.GetUUID() } });
    }

    // Portal to the arena (appears when every ember is found)
    vec3 pp(-4.0f, 0, -16.0f);
    pp.y = ground(pp.x, pp.z);
    Entity portal = AddPortal(s, pp, "Scenes/Arena.scene", { 0.35f, 0.85f, 1.0f, 1.0f });
    portal.Add<SaveableComponent>().saveTransform = false;

    AddGameManager(s, "EMBER ISLE", rig.player, portal);
    AddAudio(s, "Music", "Audio/music.wav", 0.35f, true, false);
    AddAudio(s, "Wind", "Audio/wind.wav", 0.4f, false, false);
    AddControlsCard(s, "");

    SceneSerializer(s).Save(path);
}

// ------------------------------------------------------------------ Arena
void BuildArena(const std::string& path) {
    Scene s;
    s.name = "Physics Arena";
    auto& env = s.Environment();
    env.skyTop = { 0.18f, 0.22f, 0.32f };
    env.skyHorizon = { 0.55f, 0.6f, 0.7f };
    env.fogColor = { 0.5f, 0.55f, 0.65f };
    env.fogDensity = 0.01f;
    AddSun(s, { -55, -30, 0 }, 2.4f);

    const vec4 white(1.0f);
    Box(s, "Floor", { 0, -0.5f, 0 }, { 60, 1, 60 }, { 0.78f, 0.8f, 0.83f, 1 }, "Textures/grid.png", 15.0f);
    for (int i = 0; i < 4; i++) {
        float a = i * PI * 0.5f;
        vec3 p(std::sin(a) * 30.5f, 1.0f, std::cos(a) * 30.5f);
        Box(s, "Wall", p, i % 2 ? vec3(1, 2, 62) : vec3(62, 2, 1), { 0.55f, 0.6f, 0.68f, 1 });
    }
    // Ramps (rotated static boxes)
    Box(s, "Ramp", { 10, 1.0f, 0 }, { 8, 0.5f, 4 }, { 0.9f, 0.55f, 0.3f, 1 }, "", 1.0f, BodyType::Static, { 0, 0, 16 });
    Box(s, "Ramp Top", { 17.5f, 2.08f, 0 }, { 7, 0.5f, 4 }, { 0.9f, 0.55f, 0.3f, 1 });
    Box(s, "Ramp 2", { -10, 0.9f, -8 }, { 4, 0.5f, 8 }, { 0.3f, 0.6f, 0.9f, 1 }, "", 1.0f, BodyType::Static, { -14, 0, 0 });

    // Crate pyramid
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5 - y; x++)
            Box(s, "Crate", { -4.0f + x * 1.02f + y * 0.51f, 0.5f + y * 1.01f, -14.0f }, vec3(1.0f), white, "Textures/crate.png", 1.0f, BodyType::Dynamic);
    // Tower of thin boxes
    for (int y = 0; y < 8; y++) Box(s, "Block", { 8, 0.25f + y * 0.51f, -12 }, { 2, 0.5f, 2 }, { 0.75f, 0.8f, 0.9f, 1 }, "", 1.0f, BodyType::Dynamic);

    // Moving platform
    Entity plat = Box(s, "Moving Platform", { -16, 1.5f, 6 }, { 4, 0.4f, 4 }, { 0.35f, 0.85f, 0.6f, 1 });
    plat.Add<RigidBodyComponent>().type = BodyType::Kinematic;
    AddScript(plat, "MovingPlatform", { { "Offset", vec3(0, 3, 12) }, { "Period", 7.0f } });

    // Spinning showcase pieces
    for (int i = 0; i < 3; i++) {
        Entity e = Mesh(s, "Showcase", i == 0 ? "builtin:sphere" : i == 1 ? "builtin:capsule" : "builtin:cone", { -6.0f + i * 3.0f, 1.2f, 14 },
                        vec3(1.2f), { 0.9f, 0.4f + i * 0.2f, 0.3f + i * 0.25f, 1 });
        e.Get<MeshRendererComponent>().specular = 1.0f;
        e.Get<MeshRendererComponent>().shininess = 96.0f;
        AddScript(e, "Spinner", { { "Degrees/Second", 40.0f + i * 25.0f }, { "Axis", vec3(0.3f, 1, 0) } });
    }
    // Colored point lights
    vec3 lightCols[] = { { 1, 0.3f, 0.3f }, { 0.3f, 1, 0.5f }, { 0.4f, 0.5f, 1 } };
    for (int i = 0; i < 3; i++) {
        Entity l = s.CreateEntity("Light");
        l.Transform().position = { -6.0f + i * 3.0f, 3.0f, 12 };
        auto& lc = l.Add<LightComponent>();
        lc.type = LightType::Point;
        lc.color = lightCols[i];
        lc.intensity = 2.5f;
        lc.range = 7.0f;
    }

    PlayerRig rig = AddPlayer(s, { 0, 1, 8 });
    // Ball prefab (inactive template) + launcher
    Entity ball = Mesh(s, "BallPrefab", "builtin:sphere", { 0, -50, 0 }, vec3(0.6f), { 1.0f, 0.85f, 0.2f, 1 });
    ball.Get<TagComponent>().active = false;
    auto& brb = ball.Add<RigidBodyComponent>();
    brb.mass = 0.5f;
    brb.restitution = 0.55f;
    ball.Add<SphereColliderComponent>();
    AddScript(rig.player, "BallLauncher", { { "Ball Prefab", (uint64_t)ball.GetUUID() }, { "Camera", (uint64_t)rig.camera.GetUUID() } });

    Entity portal = AddPortal(s, { 0, 0, 22 }, "Scenes/Island.scene", { 1.0f, 0.6f, 0.3f, 1.0f });
    AddGameManager(s, "PHYSICS ARENA", rig.player, portal);
    AddControlsCard(s, "Left click / RT - launch ball\n");
    AddAudio(s, "Music", "Audio/music.wav", 0.3f, true, false);

    SceneSerializer(s).Save(path);
}

} // namespace

bool BuildDemoProject(const fs::path& dir) {
    fs::create_directories(dir / "Scenes");
    json project = {
        { "name", "Ember Isle" },
        { "startScene", "Scenes/Island.scene" },
        { "scenes", { "Scenes/Island.scene", "Scenes/Arena.scene" } },
        { "inputMap", "input.json" },
        { "windowWidth", 1600 },
        { "windowHeight", 900 },
        { "fullscreen", false },
        { "vsync", true },
    };
    FileSystem::WriteText(dir / "project.json", project.dump(2));
    if (!Project::Load(dir)) return false;
    InputMap map;
    map.SetDefaults();
    FileSystem::WriteText(dir / "input.json", map.ToJson().dump(2));
    BuildIsland("Scenes/Island.scene");
    BuildArena("Scenes/Arena.scene");
    ND_INFO("Demo project written to {}", dir.string());
    return true;
}

} // namespace demo
