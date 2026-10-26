#include "ProjectTemplates.h"
#include <nd/9divine.h>
#include <nd/scene/SceneSerializer.h>
#include <nd/core/FileSystem.h>

using namespace nd;

namespace templates {

const std::vector<TemplateInfo>& All() {
    static const std::vector<TemplateInfo> list = {
        { Template::Empty, "empty", "Empty",
          "A camera, a sun and a ground plane. Start from scratch." },
        { Template::Starter3D, "starter", "3D Starter",
          "A playable third-person character with camera, physics props, HUD and pause menu." },
        { Template::Sample, "sample", "Ember Isle (Sample)",
          "A copy of the full demo game: terrain, AI, animation, audio, two scenes." },
    };
    return list;
}

bool FromKey(const std::string& key, Template& out) {
    for (auto& t : All())
        if (key == t.key) { out = t.id; return true; }
    return false;
}

static fs::path SampleDir() { return fs::path(ND_SOURCE_DIR) / "projects" / "Demo"; }
bool SampleAvailable() { return fs::exists(SampleDir() / "project.json"); }

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

void BuildStarterScene(Scene& s, const std::string& title) {
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
    bm.specular = 0.8f;
    bm.shininess = 64.0f;
    ball.Add<SphereColliderComponent>();
    auto& brb = ball.Add<RigidBodyComponent>();
    brb.mass = 0.6f;
    brb.restitution = 0.5f;

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
    Child(s, player, "Body", "builtin:capsule", { 0, 0.9f, 0 }, vec3(0.9f), { 0.95f, 0.95f, 0.97f, 1 });
    Child(s, player, "Nose", "builtin:cube", { 0, 1.35f, -0.42f }, { 0.25f, 0.18f, 0.25f }, { 0.25f, 0.75f, 0.6f, 1 });

    Entity cam = s.CreateEntity("Main Camera");
    cam.Transform().position = { 0, 3, 10 };
    cam.Add<CameraComponent>();
    Script(cam, "ThirdPersonCamera", { { "Target", (uint64_t)player.GetUUID() }, { "Distance", 6.0f } });
    Script(player, "PlayerController", { { "Camera", (uint64_t)cam.GetUUID() } });

    Entity gm = s.CreateEntity("GameManager");
    Script(gm, "GameManager", { { "Title", title }, { "Player", (uint64_t)player.GetUUID() } });
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
        if (!SampleAvailable()) { error = "The Ember Isle sample isn't available in this build."; return false; }
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
        ND_INFO("Created project '{}' from the Ember Isle sample in {}", name, dir.string());
        return true;
    }

    if (!WriteProjectFiles(dir, name)) { error = "Couldn't write project files to '" + dir.string() + "'."; return false; }
    if (!Project::Load(dir)) { error = "The new project couldn't be loaded."; return false; }
    Scene scene;
    if (t == Template::Empty) BuildEmptyScene(scene);
    else BuildStarterScene(scene, name);
    if (!SceneSerializer(scene).Save("Scenes/Main.scene")) { error = "Couldn't save the starting scene."; return false; }
    ND_INFO("Created project '{}' ({} template) in {}", name, t == Template::Empty ? "Empty" : "3D Starter", dir.string());
    return true;
}

} // namespace templates
