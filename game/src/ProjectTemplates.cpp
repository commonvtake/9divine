#include "ProjectTemplates.h"
#include "Characters.h"
#include <euphoria/Euphoria.h>
#include <euphoria/scene/SceneSerializer.h>
#include <euphoria/core/FileSystem.h>

using namespace eu;

namespace templates {

const std::vector<TemplateInfo>& All() {
    static const std::vector<TemplateInfo> list = {
        { Template::Empty, "empty", "Empty",
          "A camera, a sun and a ground plane. Start from scratch." },
        { Template::Starter3D, "starter", "3D Starter",
          "A playable third-person character with camera, physics props, HUD and pause menu." },
        { Template::Platformer2D, "platformer2d", "2D Platformer",
          "Pixel-art side scroller: a running, jumping hero, coins, slimes to stomp and a Tiled map to edit." },
        { Template::TopDown2D, "topdown2d", "2D Top-Down",
          "Pixel-art top-down adventure: a hero walking in 4 directions, a village, a pond and gems to find." },
        { Template::Sample, "sample", "Divine City (Sample)",
          "A copy of the full demo game: a city with doors and interiors, an island with AI, a physics arena." },
    };
    return list;
}

bool FromKey(const std::string& key, Template& out) {
    for (auto& t : All())
        if (key == t.key) { out = t.id; return true; }
    return false;
}

static fs::path SampleDir() { return fs::path(EU_SOURCE_DIR) / "projects" / "Demo"; }
bool SampleAvailable() { return fs::exists(SampleDir() / "project.json"); }
static fs::path Starter2DDir() { return fs::path(EU_SOURCE_DIR) / "projects" / "Starter2D"; }
bool Starter2DAvailable() { return fs::exists(Starter2DDir() / "Maps" / "Platformer.tmx"); }

bool IsValidProjectName(const std::string& name, std::string* why) {
    auto fail = [&](const char* msg) { if (why) *why = msg; return false; };
    if (name.empty()) return fail("Enter a project name.");
    if (name.size() > 64) return fail("Keep the name under 64 characters.");
    if (name.front() == ' ' || name.back() == ' ') return fail("The name can't start or end with a space.");
    for (char c : name)
        if (!(std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_'))
            return fail("Use only letters, numbers, spaces, '-' and '_'.");
    return true;
}

// ------------------------------------------------------------------ helpers
namespace {

Entity Box(Scene& s, const std::string& name, const vec3& pos, const vec3& size, const vec4& color, BodyType body = BodyType::Static) {
    Entity e = s.CreateEntity(name);
    e.Transform().position = pos;
    e.Transform().scale = size;
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = "builtin:cube";
    mr.color = color;
    e.Add<BoxColliderComponent>();
    if (body != BodyType::Static) {
        auto& rb = e.Add<RigidBodyComponent>();
        rb.type = body;
        rb.mass = size.x * size.y * size.z * 4.0f;
    }
    return e;
}

Entity Child(Scene& s, Entity parent, const std::string& name, const std::string& mesh, const vec3& pos, const vec3& scale, const vec4& color) {
    Entity e = s.CreateEntity(name);
    e.Transform().parent = parent.GetUUID();
    e.Transform().position = pos;
    e.Transform().scale = scale;
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = mesh;
    mr.color = color;
    return e;
}

void Script(Entity e, const std::string& cls, json fields = json::object()) {
    ScriptInstance si;
    si.className = cls;
    si.fields = std::move(fields);
    e.GetOrAdd<ScriptComponent>().scripts.push_back(std::move(si));
}

void AddSun(Scene& s) {
    Entity sun = s.CreateEntity("Sun");
    sun.Transform().position = { 0, 20, 0 };
    sun.Transform().SetEulerDegrees({ -48, 35, 0 });
    auto& l = sun.Add<LightComponent>();
    l.type = LightType::Directional;
    l.color = { 1.0f, 0.96f, 0.9f };
    l.intensity = 2.2f;
}

void BuildEmptyScene(Scene& s) {
    s.name = "Main";
    AddSun(s);
    Entity cam = s.CreateEntity("Main Camera");
    cam.Transform().position = { 0, 4, 10 };
    cam.Transform().SetEulerDegrees({ -18, 0, 0 });
    cam.Add<CameraComponent>();
    Box(s, "Ground", { 0, -0.5f, 0 }, { 40, 1, 40 }, { 0.72f, 0.75f, 0.78f, 1 });
}

void BuildStarterScene(Scene& s, const std::string& title, bool hero) {
    s.name = "Main";
    AddSun(s);
    Box(s, "Ground", { 0, -0.5f, 0 }, { 60, 1, 60 }, { 0.55f, 0.68f, 0.5f, 1 });

    // A few things to walk around and push
    Box(s, "Platform", { 6, 0.5f, -6 }, { 4, 1, 4 }, { 0.75f, 0.72f, 0.68f, 1 });
    Box(s, "Ramp", { 2.1f, 0.45f, -6 }, { 4, 0.3f, 3 }, { 0.75f, 0.72f, 0.68f, 1 }).Transform().SetEulerDegrees({ 0, 0, 14 });
    Box(s, "Wall", { -8, 1, -4 }, { 1, 2, 10 }, { 0.62f, 0.64f, 0.7f, 1 });
    const vec4 crate(0.86f, 0.6f, 0.32f, 1);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3 - y; x++)
            Box(s, "Crate", { -3.0f + x * 1.05f + y * 0.52f, 0.5f + y * 1.01f, -3.0f }, vec3(1.0f), crate, BodyType::Dynamic);
    Entity ball = s.CreateEntity("Ball");
    ball.Transform().position = { 3, 0.6f, 2 };
    ball.Transform().scale = vec3(1.2f);
    auto& bm = ball.Add<MeshRendererComponent>();
    bm.mesh = "builtin:sphere";
    bm.color = { 0.35f, 0.55f, 0.95f, 1 };
    bm.roughness = 0.2f;
    ball.Add<SphereColliderComponent>();
    auto& brb = ball.Add<RigidBodyComponent>();
    brb.mass = 0.6f;
    brb.restitution = 0.5f;

    // Event Trigger example (no code): stepping on the glowing pad emits "OpenGate"; the gate
    // listens for it and sinks into the ground. Open Window > Event Log while playing to watch.
    Entity pad = Box(s, "Gate Pad", { 4, 0.03f, -2 }, { 2, 0.06f, 2 }, { 0.3f, 0.9f, 1.0f, 1 });
    pad.Get<MeshRendererComponent>().emissive = { 0.4f, 1.6f, 2.0f };
    pad.Get<BoxColliderComponent>().isTrigger = true;
    pad.Get<BoxColliderComponent>().size = { 1, 30, 1 };   // tall trigger volume above the flat pad
    {
        auto& et = pad.Add<EventTriggerComponent>();
        EventRule step;
        step.when = TriggerWhen::TriggerEnter;
        step.tagFilter = "Player";
        step.action = TriggerDo::EmitEvent;
        step.param = "OpenGate";
        step.once = true;
        EventRule dim;   // the pad goes dark once used
        dim.when = TriggerWhen::Event;
        dim.whenName = "OpenGate";
        dim.action = TriggerDo::SetActive;
        dim.param = "false";
        dim.delay = 1.6f;
        et.rules = { step, dim };
    }
    Box(s, "Gate Post L", { -1.8f, 1.5f, -12 }, { 0.5f, 3, 0.5f }, { 0.62f, 0.64f, 0.7f, 1 });
    Box(s, "Gate Post R", { 1.8f, 1.5f, -12 }, { 0.5f, 3, 0.5f }, { 0.62f, 0.64f, 0.7f, 1 });
    Entity gate = Box(s, "Gate", { 0, 1.25f, -12 }, { 3.1f, 2.5f, 0.3f }, { 0.85f, 0.45f, 0.25f, 1 });
    gate.Get<MeshRendererComponent>().metallic = 1.0f;
    gate.Get<MeshRendererComponent>().roughness = 0.35f;
    {
        auto& et = gate.Add<EventTriggerComponent>();
        EventRule open;
        open.when = TriggerWhen::Event;
        open.whenName = "OpenGate";
        open.action = TriggerDo::Move;
        open.vector = { 0, -2.6f, 0 };
        open.duration = 1.5f;
        open.once = true;
        EventRule log;
        log.when = TriggerWhen::Event;
        log.whenName = "OpenGate";
        log.action = TriggerDo::Log;
        log.param = "The gate is opening!";
        log.once = true;
        et.rules = { open, log };
    }
    Entity prize = Box(s, "Prize", { 0, 0.6f, -15 }, vec3(0.8f), { 1.0f, 0.8f, 0.2f, 1 });
    prize.Get<MeshRendererComponent>().emissive = { 1.2f, 0.8f, 0.1f };
    prize.Get<MeshRendererComponent>().metallic = 1.0f;
    prize.Get<MeshRendererComponent>().roughness = 0.2f;
    Script(prize, "Spinner", { { "Degrees/Second", 60.0f } });

    // Player: physics body + capsule visual (with a nose so you can see where it faces)
    Entity player = s.CreateEntity("Player");
    player.Get<TagComponent>().tag = "Player";
    player.Transform().position = { 0, 0.05f, 4 };
    auto& rb = player.Add<RigidBodyComponent>();
    rb.friction = 0.0f;
    rb.restitution = 0.0f;
    rb.drag = 0.0f;
    auto& col = player.Add<SphereColliderComponent>();
    col.radius = 0.45f;
    col.offset = { 0, 0.45f, 0 };
    player.Add<AudioListenerComponent>();
    player.Add<SaveableComponent>();
    player.Add<NetworkIdentityComponent>();
    Entity model;
    if (hero) {
        // The animated Hero (Idle/Walk/Run/Jump), copied into the project's Models folder
        model = characters::AddHeroModel(s, player);
    } else {
        Child(s, player, "Body", "builtin:capsule", { 0, 0.9f, 0 }, vec3(0.9f), { 0.95f, 0.95f, 0.97f, 1 });
        Child(s, player, "Nose", "builtin:cube", { 0, 1.35f, -0.42f }, { 0.25f, 0.18f, 0.25f }, { 0.25f, 0.75f, 0.6f, 1 });
    }

    Entity cam = s.CreateEntity("Main Camera");
    cam.Transform().position = { 0, 3, 10 };
    cam.Add<CameraComponent>();
    Script(cam, "ThirdPersonCamera", { { "Target", (uint64_t)player.GetUUID() }, { "Distance", 6.0f } });
    json controller = { { "Camera", (uint64_t)cam.GetUUID() } };
    if (model) {
        controller["Model"] = (uint64_t)model.GetUUID();
        controller["Move Speed"] = 3.5f;
        controller["Sprint Speed"] = 7.0f;
    }
    Script(player, "PlayerController", controller);

    Entity gm = s.CreateEntity("GameManager");
    Script(gm, "GameManager", { { "Title", title }, { "Player", (uint64_t)player.GetUUID() } });
}

// ------------------------------------------------------------------ 2D templates
// The map's point objects (spawns, pickups, enemies) become entities.
vec3 MapPoint(const TilemapData& map, const TilemapObject& o, float ppu) {
    vec2 p = map.MapToLocal({ o.x, o.y }, ppu);
    return { p.x, p.y, 0.0f };
}

Entity Sprite2DEntity(Scene& s, const std::string& name, const vec3& pos, const std::string& texture, const vec2& pivot) {
    Entity e = s.CreateEntity(name);
    e.Transform().position = pos;
    auto& sr = e.Add<SpriteRendererComponent>();
    sr.texture = texture;
    sr.pivot = pivot;
    return e;
}

SpriteAnimation Anim(const std::string& name, const std::string& sheet, int first, int count, float fps, bool loop = true) {
    SpriteAnimation a;
    a.name = name;
    a.texture = sheet;
    a.frameWidth = a.frameHeight = 16;
    a.firstFrame = first;
    a.frameCount = count;
    a.fps = fps;
    a.loop = loop;
    return a;
}

void Build2DScene(Scene& s, const std::string& title, bool platformer) {
    s.name = "Main";
    auto& env = s.Environment();
    env.mode2D = true;
    env.pixelsPerUnit = 16.0f;
    env.pixelPerfect = true;
    env.referenceResolution = { 320, 180 };
    env.gravity = platformer ? vec3(0.0f, -40.0f, 0.0f) : vec3(0.0f);
    env.background2D = platformer ? vec3(0.255f, 0.651f, 0.965f) : vec3(0.31f, 0.64f, 0.31f);
    const float ppu = env.pixelsPerUnit;
    const std::string mapPath = platformer ? "Maps/Platformer.tmx" : "Maps/TopDown.tmx";

    Entity mapE = s.CreateEntity("Map");
    mapE.Add<TilemapComponent>().map = mapPath;
    auto map = Tiled::Load(Project::Resolve(mapPath).u8string());

    vec3 spawn(4.0f, 4.0f, 0.0f);
    if (map) if (auto* o = map->FindObject("PlayerStart")) spawn = MapPoint(*map, *o, ppu);

    // Player
    Entity player = Sprite2DEntity(s, "Player", spawn, platformer ? "Art/hero_platformer.png" : "Art/hero_topdown.png", { 0.5f, 0.0f });
    player.Get<TagComponent>().tag = "Player";
    player.Get<SpriteRendererComponent>().region = { 0, 0, 16, 16 };
    auto& anim = player.Add<SpriteAnimatorComponent>();
    if (platformer) {
        const std::string sheet = "Art/hero_platformer.png";   // 6 columns: idle, run, jump/fall rows
        anim.animations = { Anim("idle", sheet, 0, 2, 3.0f), Anim("run", sheet, 6, 6, 12.0f), Anim("jump", sheet, 12, 1, 1.0f), Anim("fall", sheet, 13, 1, 1.0f) };
    } else {
        const std::string sheet = "Art/hero_topdown.png";      // 4 columns: down, up, right rows (left = right mirrored)
        anim.animations = { Anim("idle_down", sheet, 0, 1, 1.0f), Anim("idle_up", sheet, 4, 1, 1.0f), Anim("idle_right", sheet, 8, 1, 1.0f),
                            Anim("walk_down", sheet, 0, 4, 8.0f), Anim("walk_up", sheet, 4, 4, 8.0f), Anim("walk_right", sheet, 8, 4, 8.0f) };
        anim.defaultAnimation = "idle_down";
    }
    auto& rb = player.Add<RigidBodyComponent>();
    rb.mass = 1.0f;
    rb.drag = 0.0f;
    rb.friction = 0.0f;
    rb.restitution = 0.0f;
    if (!platformer) rb.gravityScale = 0.0f;
    auto& box = player.Add<BoxColliderComponent>();
    box.size = platformer ? vec3(0.55f, 0.8f, 1.0f) : vec3(0.6f, 0.45f, 1.0f);   // top-down: just the feet, so heads overlap walls nicely
    box.offset = { 0.0f, box.size.y * 0.5f, 0.0f };
    Script(player, "PlayerController2D", { { "Top Down", !platformer }, { "Move Speed", platformer ? 7.0f : 4.5f },
                                           { "Move Animation", platformer ? "run" : "walk" } });

    // Camera
    Entity cam = s.CreateEntity("Main Camera");
    cam.Transform().position = { spawn.x, spawn.y, 10.0f };
    auto& cc = cam.Add<CameraComponent>();
    cc.orthographic = true;
    cc.orthoSize = env.referenceResolution.y / ppu;
    cc.nearClip = 0.1f;
    cc.farClip = 100.0f;
    Script(cam, "CameraFollow2D", { { "Target", (uint64_t)player.GetUUID() }, { "Look Ahead", platformer ? 1.5f : 0.0f },
                                    { "Offset", json::array({ 0.0f, platformer ? 1.5f : 0.5f, 0.0f }) } });

    // Pickups and enemies from the map's point objects
    if (map) {
        for (auto& l : map->layers)
            for (auto& o : l.objects) {
                if (o.type == "Coin" || o.type == "Gem") {
                    bool coin = o.type == "Coin";
                    vec3 p = MapPoint(*map, o, ppu) + vec3(0.0f, 0.5f, 0.0f);
                    Entity e = Sprite2DEntity(s, o.name, p, coin ? "Art/coin.png" : "Art/gem.png", { 0.5f, 0.5f });
                    e.Get<SpriteRendererComponent>().region = { 0, 0, 16, 16 };
                    auto& a = e.Add<SpriteAnimatorComponent>();
                    a.animations = { Anim("spin", coin ? "Art/coin.png" : "Art/gem.png", 0, 4, coin ? 8.0f : 5.0f) };
                    auto& c = e.Add<BoxColliderComponent>();
                    c.size = { 0.6f, 0.6f, 1.0f };
                    c.isTrigger = true;
                    Script(e, "Collectible2D", { { "Sound", "Audio/coin.wav" } });
                } else if (o.type == "Enemy") {
                    Entity e = Sprite2DEntity(s, o.name, MapPoint(*map, o, ppu), "Art/slime.png", { 0.5f, 0.0f });
                    e.Get<TagComponent>().tag = "Enemy";
                    e.Get<SpriteRendererComponent>().region = { 0, 0, 16, 16 };
                    e.Add<SpriteAnimatorComponent>().animations = { Anim("idle", "Art/slime.png", 0, 2, 3.0f) };
                    auto& erb = e.Add<RigidBodyComponent>();
                    erb.drag = 0.0f;
                    erb.friction = 0.0f;
                    erb.restitution = 0.0f;
                    auto& eb = e.Add<BoxColliderComponent>();
                    eb.size = { 0.75f, 0.55f, 1.0f };
                    eb.offset = { 0.0f, 0.275f, 0.0f };
                    Script(e, "Patrol2D");
                }
            }
    }

    Entity hud = s.CreateEntity("HUD");
    Script(hud, "Hud2D", { { "Title", title }, { "Item Name", platformer ? "COINS" : "GEMS" },
                           { "Icon", platformer ? "Art/coin.png" : "Art/gem.png" },
                           { "Controls", platformer ? "A/D or arrows: run    Space: jump (hold for higher)    Land on slimes!"
                                                    : "WASD or arrows: walk    Find all the gems" } });
}

bool Copy2DAssets(const fs::path& dir) {
    std::error_code ec;
    for (const char* sub : { "Art", "Maps" })
        fs::copy(Starter2DDir() / sub, dir / sub, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    // A few sounds from the sample
    for (const char* f : { "jump.wav", "coin.wav", "hit.wav", "enemy_die.wav" }) {
        std::error_code cec;
        fs::copy_file(SampleDir() / "Audio" / f, dir / "Audio" / f, fs::copy_options::overwrite_existing, cec);
    }
    return true;
}

bool WriteProjectFiles(const fs::path& dir, const std::string& name) {
    for (const char* sub : { "Scenes", "Models", "Textures", "Audio" }) fs::create_directories(dir / sub);
    json project = {
        { "name", name },
        { "startScene", "Scenes/Main.scene" },
        { "scenes", { "Scenes/Main.scene" } },
        { "inputMap", "input.json" },
        { "windowWidth", 1280 },
        { "windowHeight", 720 },
        { "fullscreen", false },
        { "vsync", true },
    };
    if (!FileSystem::WriteText(dir / "project.json", project.dump(2))) return false;
    InputMap map;
    map.SetDefaults();
    FileSystem::WriteText(dir / "input.json", map.ToJson().dump(2));
    FileSystem::WriteText(dir / ".gitignore", "Builds/\n");
    return true;
}

bool IsEmptyOrMissing(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return true;
    return fs::is_directory(dir, ec) && fs::is_empty(dir, ec);
}

} // namespace

// ------------------------------------------------------------------ create
bool CreateProject(const fs::path& dirIn, const std::string& name, Template t, std::string& error) {
    if (!IsValidProjectName(name, &error)) return false;
    fs::path dir = fs::absolute(dirIn).lexically_normal();
    if (!IsEmptyOrMissing(dir)) {
        error = "The folder '" + dir.string() + "' already exists and isn't empty.";
        return false;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error = "Couldn't create '" + dir.string() + "': " + ec.message();
        return false;
    }

    if (t == Template::Sample) {
        if (!SampleAvailable()) { error = "The sample project isn't available in this build."; return false; }
        fs::copy(SampleDir(), dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { error = "Couldn't copy the sample: " + ec.message(); return false; }
        auto text = FileSystem::ReadText(dir / "project.json");
        try {
            json j = json::parse(text ? *text : "{}");
            j["name"] = name;
            FileSystem::WriteText(dir / "project.json", j.dump(2));
        } catch (...) {}
        FileSystem::WriteText(dir / ".gitignore", "Builds/\n");
        if (!Project::Load(dir)) { error = "The copied sample couldn't be loaded."; return false; }
        EU_INFO("Created project '{}' from the sample in {}", name, dir.string());
        return true;
    }

    if (!WriteProjectFiles(dir, name)) { error = "Couldn't write project files to '" + dir.string() + "'."; return false; }
    if (!Project::Load(dir)) { error = "The new project couldn't be loaded."; return false; }
    Scene scene;
    if (t == Template::Platformer2D || t == Template::TopDown2D) {
        if (!Starter2DAvailable() || !Copy2DAssets(dir)) { error = "The 2D template's art isn't available in this build."; return false; }
        Build2DScene(scene, name, t == Template::Platformer2D);
    } else if (t == Template::Empty) BuildEmptyScene(scene);
    else {
        // Give the starter the animated Hero when the sample's model is available.
        bool hero = false;
        fs::path src = SampleDir() / "Models" / "Hero";
        if (fs::exists(src / "Hero.glb")) {
            std::error_code cec;
            fs::create_directories(dir / "Models" / "Hero", cec);
            fs::copy(src, dir / "Models" / "Hero", fs::copy_options::recursive | fs::copy_options::overwrite_existing, cec);
            hero = !cec;
        }
        BuildStarterScene(scene, name, hero);
    }
    if (!SceneSerializer(scene).Save("Scenes/Main.scene")) { error = "Couldn't save the starting scene."; return false; }
    const char* kind = "3D Starter";
    for (auto& info : All()) if (info.id == t) kind = info.name;
    EU_INFO("Created project '{}' ({} template) in {}", name, kind, dir.string());
    return true;
}

} // namespace templates
