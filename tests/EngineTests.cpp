// Headless unit tests for engine systems and the game's gameplay scripts (no window or GPU needed).
// Run:  ctest --output-on-failure    or    ./euphoria-tests
#include <euphoria/Euphoria.h>
#include <euphoria/scene/SceneSerializer.h>
#include <euphoria/core/Json.h>
#include <euphoria/renderer/RayTracer.h>
#include "Interaction.h"   // game/src: doors and other interactables
#include <euphoria/animation/Retarget.h>
#include <euphoria/assets/TextureFinder.h>
#include <euphoria/assets/ModelImport.h>
#include <euphoria/core/Xml.h>
#include <fstream>
#include "PlayerCombat.h"  // game/src: hack-and-slash combat
#include "Characters.h"
#include <cstdio>
#include <cmath>

using namespace eu;

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(std::abs((a) - (b)) <= (eps))

struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& Tests() { static std::vector<TestCase> t; return t; }
struct Register { Register(const char* n, void (*f)()) { Tests().push_back({ n, f }); } };
#define TEST(name) static void name(); static Register reg_##name(#name, name); static void name()

// ---------------------------------------------------------------------- ECS
TEST(ECS_CreateAddViewRemove) {
    Registry r;
    EntityId a = r.Create(), b = r.Create(), c = r.Create();
    r.Add<TransformComponent>(a).position = { 1, 0, 0 };
    r.Add<TransformComponent>(b);
    r.Add<TransformComponent>(c);
    r.Add<LightComponent>(b);
    int both = 0;
    r.View<TransformComponent, LightComponent>([&](EntityId id, TransformComponent&, LightComponent&) { both++; CHECK(id == b); });
    CHECK(both == 1);
    CHECK(r.Get<TransformComponent>(a).position.x == 1.0f);
    r.Remove<TransformComponent>(a);
    CHECK(!r.Has<TransformComponent>(a));
    CHECK(r.Has<TransformComponent>(c));   // swap-remove kept the others intact
    CHECK(r.Count<TransformComponent>() == 2);
}

TEST(ECS_StaleHandlesAreDetected) {
    Registry r;
    EntityId a = r.Create();
    r.Destroy(a);
    EntityId b = r.Create();             // reuses the slot with a new version
    CHECK(!r.Valid(a));
    CHECK(r.Valid(b));
    CHECK(ecs::Index(a) == ecs::Index(b));
}

TEST(ECS_ReferencesStableWhenPoolGrows) {
    Registry r;
    EntityId first = r.Create();
    auto& t = r.Add<TransformComponent>(first);
    t.position = { 7, 7, 7 };
    for (int i = 0; i < 5000; i++) r.Add<TransformComponent>(r.Create());
    CHECK(t.position.x == 7.0f);         // deque storage: no reallocation of existing elements
}

// ---------------------------------------------------------------------- Core
TEST(Core_Base64RoundTrip) {
    std::vector<uint8_t> data(1000);
    for (size_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i * 37 + 11);
    auto enc = Base64Encode(data.data(), data.size());
    CHECK(Base64Decode(enc) == data);
}

TEST(Core_Format) {
    CHECK(Format("{} + {} = {}", 1, 2, 3) == "1 + 2 = 3");
}

// ---------------------------------------------------------------------- Scene
TEST(Scene_HierarchyWorldMatrix) {
    Scene s;
    Entity parent = s.CreateEntity("Parent");
    Entity child = s.CreateEntity("Child");
    parent.Transform().position = { 10, 0, 0 };
    parent.Transform().SetEulerDegrees({ 0, 90, 0 });
    child.Transform().position = { 0, 0, -2 };
    child.Transform().parent = parent.GetUUID();
    vec3 w = s.WorldPosition(child);
    CHECK_NEAR(w.x, 8.0f, 1e-4f);        // -Z rotated 90 deg about Y points to -X
    CHECK_NEAR(w.z, 0.0f, 1e-4f);
    s.SetParent(child, Entity(), true);  // unparent, keep world position
    CHECK_NEAR(child.Transform().position.x, 8.0f, 1e-4f);
    CHECK(s.GetChildren(parent).empty());
}

TEST(Scene_SerializationRoundTrip) {
    Scene a;
    Entity e = a.CreateEntity("Thing");
    e.Transform().position = { 1, 2, 3 };
    auto& mr = e.Add<MeshRendererComponent>();
    mr.mesh = "builtin:sphere";
    mr.color = { 0.1f, 0.2f, 0.3f, 1.0f };
    auto& l = e.Add<LightComponent>();
    l.type = LightType::Spot;
    l.range = 42.0f;
    auto& anim = e.Add<AnimatorComponent>();
    anim.states.push_back({ "Run", "RunClip", 1.5f, true, "", {} });
    auto& sc = e.Add<ScriptComponent>();
    ScriptInstance si; si.className = "Spinner"; si.fields = { { "Speed", 3.0 } };
    sc.scripts.push_back(si);

    json j = SceneSerializer(a).ToJson();
    Scene b;
    SceneSerializer(b).FromJson(j);
    Entity f = b.FindByName("Thing");
    CHECK(f.IsValid());
    CHECK(f.GetUUID() == e.GetUUID());
    CHECK(f.Transform().position == vec3(1, 2, 3));
    CHECK(f.Get<MeshRendererComponent>().mesh == "builtin:sphere");
    CHECK(f.Get<LightComponent>().type == LightType::Spot);
    CHECK(f.Get<LightComponent>().range == 42.0f);
    CHECK(f.Get<AnimatorComponent>().states.size() == 1);
    CHECK(f.Get<ScriptComponent>().scripts[0].fields["Speed"] == 3.0);
    CHECK(SceneSerializer(b).ToJson() == j);   // stable
}

TEST(Scene_DuplicateRemapsHierarchy) {
    Scene s;
    Entity root = s.CreateEntity("Root");
    Entity kid = s.CreateEntity("Kid");
    kid.Transform().parent = root.GetUUID();
    Entity copy = s.DuplicateEntity(root);
    CHECK(copy.IsValid() && copy != root);
    auto kids = s.GetChildren(copy);
    CHECK(kids.size() == 1);
    CHECK(kids[0] != kid);
    CHECK(s.GetChildren(root).size() == 1);
}

// ---------------------------------------------------------------------- Physics
static Entity MakeGround(Scene& s) {
    Entity g = s.CreateEntity("Ground");
    g.Transform().position = { 0, -0.5f, 0 };
    g.Add<BoxColliderComponent>().size = { 20, 1, 20 };
    return g;
}

TEST(Physics_SphereComesToRestOnBox) {
    Scene s;
    MakeGround(s);
    Entity ball = s.CreateEntity("Ball");
    ball.Transform().position = { 0, 5, 0 };
    ball.Add<SphereColliderComponent>().radius = 0.5f;
    ball.Add<RigidBodyComponent>().restitution = 0.0f;
    for (int i = 0; i < 300; i++) s.Physics().Step(1.0f / 60.0f);
    CHECK_NEAR(ball.Transform().position.y, 0.5f, 0.05f);
    CHECK(std::abs(ball.Get<RigidBodyComponent>().velocity.y) < 0.5f);
    CHECK(ball.Get<RigidBodyComponent>().grounded);
}

TEST(Physics_BoxStackStaysUp) {
    Scene s;
    MakeGround(s);
    std::vector<Entity> boxes;
    for (int i = 0; i < 4; i++) {
        Entity b = s.CreateEntity("Box");
        b.Transform().position = { 0, 0.5f + i * 1.01f, 0 };
        b.Add<BoxColliderComponent>();
        b.Add<RigidBodyComponent>().restitution = 0.0f;
        boxes.push_back(b);
    }
    for (int i = 0; i < 240; i++) s.Physics().Step(1.0f / 60.0f);
    CHECK_NEAR(boxes.back().Transform().position.y, 3.5f, 0.15f);
    CHECK_NEAR(boxes.back().Transform().position.x, 0.0f, 0.05f);
}

TEST(Physics_TriggerEvents) {
    Scene s;
    Entity zone = s.CreateEntity("Zone");
    auto& c = zone.Add<BoxColliderComponent>();
    c.size = vec3(2.0f);
    c.isTrigger = true;
    Entity ball = s.CreateEntity("Ball");
    ball.Transform().position = { 0, 3, 0 };
    ball.Add<SphereColliderComponent>().radius = 0.25f;
    ball.Add<RigidBodyComponent>();
    bool entered = false, exited = false;
    for (int i = 0; i < 120; i++) {
        s.Physics().Step(1.0f / 60.0f);
        for (auto& ev : s.Physics().Events()) {
            if (ev.type == CollisionEvent::TriggerEnter) entered = true;
            if (ev.type == CollisionEvent::TriggerExit) exited = true;
        }
        s.Physics().ClearEvents();
    }
    CHECK(entered);
    CHECK(exited);   // fell all the way through the trigger
}

TEST(Physics_RaycastAndOverlap) {
    Scene s;
    Entity box = s.CreateEntity("Box");
    box.Transform().position = { 0, 0, -10 };
    box.Add<BoxColliderComponent>().size = vec3(2.0f);
    RaycastHit hit;
    CHECK(s.Physics().Raycast({ vec3(0.0f), vec3(0, 0, -1) }, 100.0f, hit));
    CHECK(hit.entity == box);
    CHECK_NEAR(hit.distance, 9.0f, 1e-3f);
    CHECK_NEAR(hit.normal.z, 1.0f, 1e-3f);
    CHECK(!s.Physics().Raycast({ vec3(0.0f), vec3(0, 0, 1) }, 100.0f, hit));
    CHECK(s.Physics().OverlapSphere({ 0, 0, -8.5f }, 1.0f).size() == 1);
}

TEST(Physics_TerrainCollision) {
    Scene s;
    Entity t = s.CreateEntity("Terrain");
    auto& tc = t.Add<TerrainComponent>();
    tc.resolution = 33;
    tc.size = 40.0f;
    tc.heights.assign(33 * 33, 2.0f);   // flat at y = 2
    Entity ball = s.CreateEntity("Ball");
    ball.Transform().position = { 3, 10, 3 };
    ball.Add<SphereColliderComponent>().radius = 0.5f;
    ball.Add<RigidBodyComponent>().restitution = 0.0f;
    for (int i = 0; i < 300; i++) s.Physics().Step(1.0f / 60.0f);
    CHECK_NEAR(ball.Transform().position.y, 2.5f, 0.05f);
    RaycastHit hit;
    CHECK(s.Physics().Raycast({ vec3(-5, 20, -5), glm::normalize(vec3(0.3f, -1, 0.2f)) }, 100.0f, hit));
    CHECK(hit.hitTerrain);
    CHECK_NEAR(hit.point.y, 2.0f, 0.05f);
}

// ---------------------------------------------------------------------- Terrain
TEST(Terrain_GenerateAndSample) {
    TerrainComponent t;
    t.resolution = 65;
    TerrainSystem::Generate(t);
    CHECK(t.heights.size() == 65u * 65u);
    // Bilinear sampling matches grid points exactly
    float step = t.size / 64.0f;
    float h = t.HeightAtLocal(-t.size * 0.5f + 10 * step, -t.size * 0.5f + 20 * step);
    CHECK_NEAR(h, t.heights[20 * 65 + 10], 1e-4f);
    float before = t.HeightAtLocal(0, 0);
    TerrainSystem::Sculpt(t, { 0, 0 }, 5.0f, 1.0f, SculptMode::Raise);
    CHECK(t.HeightAtLocal(0, 0) > before);
}

// ---------------------------------------------------------------------- Navigation
TEST(Nav_PathAroundWall) {
    Scene s;
    MakeGround(s);
    Entity wall = s.CreateEntity("Wall");
    wall.Transform().position = { 0, 1, 0 };
    wall.Add<BoxColliderComponent>().size = { 1, 2, 12 };
    auto& ns = s.Nav().settings;
    ns.autoBounds = false;
    ns.boundsMin = { -10, -2, -10 };
    ns.boundsMax = { 10, 5, 10 };
    ns.cellSize = 0.5f;
    s.Nav().Bake();
    CHECK(s.Nav().IsBaked());
    float top = 0.0f;
    CHECK(s.Nav().SampleHeight({ 0, 0, 0 }, top));   // the wall's top is a surface...
    CHECK_NEAR(top, 2.0f, 0.01f);                    // ...but too high to step onto
    std::vector<vec3> path;
    CHECK(s.Nav().FindPath({ -5, 0, 0 }, { 5, 0, 0 }, path));
    CHECK(path.size() >= 3);                       // must bend around the wall
    for (auto& p : path) CHECK(s.Nav().IsWalkable(p) && p.y < 0.5f);   // never on top of the wall
    bool goesAround = false;
    for (auto& p : path) if (std::abs(p.z) > 5.5f) goesAround = true;
    CHECK(goesAround);
}

// ---------------------------------------------------------------------- AI
TEST(AI_BehaviourTree) {
    int actionTicks = 0;
    bool flag = false;
    auto tree = BT::Selector({
        BT::Sequence({ BT::Condition([&] { return flag; }), BT::Action([&](float) { actionTicks++; return BTStatus::Success; }) }),
        BT::Action([](float) { return BTStatus::Running; }),
    });
    CHECK(tree->Tick(0.1f) == BTStatus::Running);  // condition false -> fallback branch
    CHECK(actionTicks == 0);
    flag = true;
    CHECK(tree->Tick(0.1f) == BTStatus::Success);
    CHECK(actionTicks == 1);
    auto wait = BT::Wait(0.25f);
    CHECK(wait->Tick(0.1f) == BTStatus::Running);
    CHECK(wait->Tick(0.2f) == BTStatus::Success);
}

TEST(AI_StateMachine) {
    std::string log;
    StateMachine fsm;
    fsm.Add("A", { [&] { log += "a+"; }, [&](float) { log += "a"; }, [&] { log += "a-"; } });
    fsm.Add("B", { [&] { log += "b+"; }, nullptr, nullptr });
    fsm.Change("A");
    fsm.Update(0.1f);
    fsm.Change("B");
    CHECK(log == "a+aa-b+");
    CHECK(fsm.Current() == "B");
}

// ---------------------------------------------------------------------- Animation
TEST(Animation_SampleAndBlend) {
    Skeleton skel;
    skel.joints.push_back({ "root", -1, mat4(1.0f), vec3(0), quat(1, 0, 0, 0), vec3(1) });
    AnimationClip clip;
    clip.duration = 1.0f;
    JointChannel ch;
    ch.joint = 0;
    ch.translation.times = { 0.0f, 1.0f };
    ch.translation.values = { vec3(0), vec3(10, 0, 0) };
    clip.channels.push_back(ch);
    Pose p;
    SampleClip(clip, skel, 0.25f, p);
    CHECK_NEAR(p[0].t.x, 2.5f, 1e-4f);
    Pose a = p, b, out;
    SampleClip(clip, skel, 1.0f, b);
    BlendPoses(a, b, 0.5f, out);
    CHECK_NEAR(out[0].t.x, 6.25f, 1e-4f);
    std::vector<mat4> globals, skin;
    ComputeSkinMatrices(skel, out, globals, skin);
    CHECK_NEAR(skin[0][3].x, 6.25f, 1e-4f);
}

// ---------------------------------------------------------------------- Input / Save
TEST(Input_MapJsonRoundTrip) {
    InputMap m;
    m.SetDefaults();
    m.FindAction("Jump")->bindings.push_back({ BindingType::GamepadButton, Gamepad::Y, 1.0f });
    InputMap n;
    n.FromJson(m.ToJson());
    CHECK(n.actions.size() == m.actions.size());
    CHECK(n.FindAction("Jump")->bindings.size() == 3);
    CHECK(n.FindAxis("MoveY")->bindings.back().scale == -1.0f);
}

TEST(Save_ValuesAndSceneSnapshot) {
    SaveGame::Clear();
    SaveGame::SetInt("coins", 42);
    SaveGame::SetVec3("spawn", { 1, 2, 3 });
    CHECK(SaveGame::Save(7));
    SaveGame::Clear();
    CHECK(SaveGame::GetInt("coins", -1) == -1);
    CHECK(SaveGame::Load(7));
    CHECK(SaveGame::GetInt("coins") == 42);
    CHECK(SaveGame::GetVec3("spawn") == vec3(1, 2, 3));
    SaveGame::DeleteSlot(7);
    CHECK(!SaveGame::SlotExists(7));

    Scene s;
    Entity keep = s.CreateEntity("Keep");
    keep.Add<SaveableComponent>();
    keep.Transform().position = { 5, 0, 0 };
    Entity coin = s.CreateEntity("Coin");
    coin.Add<SaveableComponent>();
    // Capture a state where the coin was already collected:
    s.DestroyEntity(coin);
    SaveGame::CaptureScene(s);
    Scene fresh;
    Entity k2 = fresh.CreateEntityWithUUID(keep.GetUUID(), "Keep");
    k2.Add<SaveableComponent>();
    Entity c2 = fresh.CreateEntityWithUUID(UUID(), "Coin");
    c2.Add<SaveableComponent>();
    SaveGame::RestoreScene(fresh);
    CHECK(k2.Transform().position.x == 5.0f);
    CHECK(!fresh.FindByName("Coin"));   // collected things stay collected
}

// ---------------------------------------------------------------------- Toon shading
// A unit cube with split (flat) normals: 6 faces x 4 vertices, like Primitives::Cube().
static void FlatCube(std::vector<Vertex>& verts, std::vector<uint32_t>& idx) {
    const vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const vec3& n : normals) {
        vec3 u = std::abs(n.y) > 0.5f ? vec3(1, 0, 0) : vec3(0, 1, 0);
        vec3 v = glm::cross(n, u);
        uint32_t base = (uint32_t)verts.size();
        for (int i = 0; i < 4; i++) {
            float a = (i == 0 || i == 3) ? -0.5f : 0.5f, b = (i < 2) ? -0.5f : 0.5f;
            Vertex vx;
            vx.position = n * 0.5f + u * a + v * b;
            vx.normal = n;
            verts.push_back(vx);
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
}

TEST(Toon_OutlineNormalsWeldHardEdges) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    FlatCube(verts, idx);
    auto n = Mesh::ComputeOutlineNormals(verts, idx);
    CHECK(n.size() == verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        // Every cube corner should point diagonally out of the corner, e.g. (1,1,1)/sqrt(3),
        // regardless of which face's copy of the vertex it is - so the hull can't split open.
        vec3 expected = glm::normalize(glm::sign(verts[i].position));
        CHECK_NEAR(glm::dot(n[i], expected), 1.0f, 1e-4f);
        CHECK_NEAR(glm::length(n[i]), 1.0f, 1e-4f);
    }
}

TEST(Toon_OutlineNormalsKeepSmoothSurfaces) {
    // A single flat quad has no hard edges: outline normals equal the face normal.
    std::vector<Vertex> verts(4);
    verts[0].position = { 0, 0, 0 }; verts[1].position = { 1, 0, 0 };
    verts[2].position = { 1, 0, -1 }; verts[3].position = { 0, 0, -1 };
    std::vector<uint32_t> idx = { 0, 1, 2, 0, 2, 3 };
    auto n = Mesh::ComputeOutlineNormals(verts, idx);
    for (auto& v : n) CHECK_NEAR(glm::dot(v, vec3(0, 1, 0)), 1.0f, 1e-5f);
    // Degenerate input falls back to the vertex normal instead of producing NaNs.
    std::vector<Vertex> lone(1);
    lone[0].normal = { 0, 0, 1 };
    auto f = Mesh::ComputeOutlineNormals(lone, {});
    CHECK(f[0] == vec3(0, 0, 1));
}

TEST(Toon_SettingsSerialize) {
    Scene a;
    Entity plain = a.CreateEntity("Plain");
    plain.Add<MeshRendererComponent>();
    Entity hero = a.CreateEntity("Hero");
    auto& mr = hero.Add<MeshRendererComponent>();
    mr.toon = true;
    mr.toonBands = 2;
    mr.toonSoftness = 0.0f;
    mr.toonShadowColor = { 0.2f, 0.1f, 0.4f };
    mr.toonShadowFill = 0.75f;
    mr.toonRim = 0.6f;
    mr.outline = true;
    mr.outlineWidth = 4.0f;
    mr.outlineColor = { 1.0f, 0.0f, 0.0f, 1.0f };

    json j = SceneSerializer(a).ToJson();
    Scene b;
    SceneSerializer(b).FromJson(j);
    auto& p = b.FindByName("Plain").Get<MeshRendererComponent>();
    CHECK(!p.toon && !p.outline);
    auto& h = b.FindByName("Hero").Get<MeshRendererComponent>();
    CHECK(h.toon);
    CHECK(h.toonBands == 2);
    CHECK(h.toonSoftness == 0.0f);
    CHECK(h.toonShadowColor == vec3(0.2f, 0.1f, 0.4f));
    CHECK(h.toonShadowFill == 0.75f);
    CHECK(h.toonRim == 0.6f);
    CHECK(h.outline);
    CHECK(h.outlineWidth == 4.0f);
    CHECK(h.outlineColor == vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(SceneSerializer(b).ToJson() == j);
    // Meshes without toon settings don't gain a "toon" block (existing scenes stay byte-identical).
    std::string dump = j.dump();
    size_t count = 0;
    for (size_t pos = dump.find("\"toon\""); pos != std::string::npos; pos = dump.find("\"toon\"", pos + 1)) count++;
    CHECK(count == 1);
}

// ---------------------------------------------------------------------- Locomotion auto-setup
TEST(Locomotion_FindsClipsByName) {
    // Blender's exporter (with or without the "Armature|" prefix)
    auto b = FindLocomotionClips({ "Armature|Idle", "Armature|Walk", "Armature|Run", "Armature|Jump" });
    CHECK(b.idle == "Armature|Idle" && b.walk == "Armature|Walk" && b.run == "Armature|Run" && b.jump == "Armature|Jump");
    // The Khronos fox: "Survey" counts as idle
    auto f = FindLocomotionClips({ "Survey", "Walk", "Run" });
    CHECK(f.idle == "Survey" && f.walk == "Walk" && f.run == "Run" && f.jump.empty());
    // Exact names beat partial ones; case doesn't matter; "Walk_Run" isn't taken as the run
    auto m = FindLocomotionClips({ "idle_old", "IDLE", "Walk_Run", "jog", "Falling" });
    CHECK(m.idle == "IDLE");
    CHECK(m.run == "jog");
    CHECK(m.fall == "Falling");
    CHECK(!FindLocomotionClips({ "Wave", "Dance" }).Usable());
}

TEST(Locomotion_BuildsController) {
    AnimatorComponent a;
    CHECK(SetupLocomotion(a, { "Idle", "Walk", "Run", "Jump" }, 2.0f, 6.0f));
    CHECK(a.defaultState == "Locomotion");
    CHECK(a.states.size() == 2);
    CHECK(a.states[0].blendParam == "Speed");
    CHECK(a.states[0].blend.size() == 3);
    CHECK(a.states[0].blend[1].clip == "Walk" && a.states[0].blend[1].threshold == 2.0f);
    CHECK(a.states[0].blend[2].clip == "Run" && a.states[0].blend[2].threshold == 6.0f);
    CHECK(a.states[1].name == "Airborne" && a.states[1].clip == "Jump" && !a.states[1].loop);
    CHECK(a.transitions.size() == 2);
    CHECK(a.GetFloat("Grounded") == 1.0f);   // starts grounded, so an undriven animator idles

    // A model with only a walk cycle gets a single-clip state and no airborne state
    AnimatorComponent w;
    CHECK(SetupLocomotion(w, { "Walk" }));
    CHECK(w.states.size() == 1 && w.states[0].clip == "Walk" && w.states[0].blend.empty());
    CHECK(w.transitions.empty());

    // Nothing usable: the animator is left alone
    AnimatorComponent keep;
    keep.defaultState = "Mine";
    CHECK(!SetupLocomotion(keep, { "Dance" }));
    CHECK(keep.defaultState == "Mine");
}

// ---------------------------------------------------------------------- Transform cache
TEST(Scene_TransformCacheMatchesUncached) {
    Scene s;
    Entity root = s.CreateEntity("Root");
    root.Transform().position = { 5, 0, 0 };
    root.Transform().SetEulerDegrees({ 0, 45, 0 });
    Entity mid = s.CreateEntity("Mid");
    mid.Transform().parent = root.GetUUID();
    mid.Transform().position = { 0, 2, -1 };
    mid.Transform().scale = vec3(2.0f);
    Entity leaf = s.CreateEntity("Leaf");
    leaf.Transform().parent = mid.GetUUID();
    leaf.Transform().position = { 1, 0, 0 };
    Entity loose = s.CreateEntity("Loose");
    mid.Get<TagComponent>().active = false;

    s.CacheTransforms();
    for (Entity e : { root, mid, leaf, loose }) {
        mat4 a = s.WorldMatrix(e), b = s.CachedWorld(e.Id());
        for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) CHECK_NEAR(a[c][r], b[c][r], 1e-5f);
        CHECK(s.CachedActive(e.Id()) == s.IsActiveInHierarchy(e));
    }
    CHECK(!s.CachedActive(leaf.Id()));          // inactive parent hides the child

    // A snapshot: moving the root needs a new CacheTransforms() call
    root.Transform().position = { -3, 0, 0 };
    s.CacheTransforms();
    CHECK_NEAR(s.CachedWorld(leaf.Id())[3].x, s.WorldMatrix(leaf)[3].x, 1e-5f);

    // A parent cycle (corrupt file) must not hang
    root.Transform().parent = leaf.GetUUID();
    s.CacheTransforms();
    CHECK(true);
}

// ---------------------------------------------------------------------- Ray tracing BVH
TEST(RayTracing_BVHMatchesBruteForce) {
    // 2000 random triangles in a 20 m cube; 500 random rays. The BVH must return exactly the
    // closest hit a brute-force loop finds.
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    std::vector<vec3> a, b, c;
    for (int i = 0; i < 2000; i++) {
        vec3 p(rnd() * 20 - 10, rnd() * 20 - 10, rnd() * 20 - 10);
        a.push_back(p);
        b.push_back(p + vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 1.5f);
        c.push_back(p + vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 1.5f);
    }
    BVH bvh;
    bvh.Build(a, b, c);
    CHECK(bvh.nodes.size() > 1);
    CHECK(bvh.triIndex.size() == a.size());
    CHECK(bvh.Depth() < 40);
    // Every triangle appears exactly once in the leaves
    std::vector<int> seen(a.size(), 0);
    for (auto& n : bvh.nodes)
        if (n.count) for (uint32_t i = 0; i < n.count; i++) seen[bvh.triIndex[n.leftFirst + i]]++;
    bool once = true;
    for (int s : seen) once &= s == 1;
    CHECK(once);

    int mismatches = 0, hits = 0;
    for (int r = 0; r < 500; r++) {
        vec3 o(rnd() * 30 - 15, rnd() * 30 - 15, rnd() * 30 - 15);
        vec3 d = glm::normalize(vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f));
        float best = -1.0f;
        for (size_t t = 0; t < a.size(); t++) {
            vec3 e1 = b[t] - a[t], e2 = c[t] - a[t], p = glm::cross(d, e2);
            float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-9f) continue;
            vec3 s = o - a[t];
            float u = glm::dot(s, p) / det;
            if (u < 0 || u > 1) continue;
            vec3 q = glm::cross(s, e1);
            float v = glm::dot(d, q) / det;
            if (v < 0 || u + v > 1) continue;
            float h = glm::dot(e2, q) / det;
            if (h > 1e-5f && (best < 0 || h < best)) best = h;
        }
        float got = bvh.Intersect(o, d, a, b, c);
        if (best > 0) hits++;
        if ((best < 0) != (got < 0) || (best > 0 && std::abs(best - got) > 1e-4f)) mismatches++;
    }
    CHECK(hits > 50);          // the test actually exercises hits
    CHECK(mismatches == 0);
}

// ---------------------------------------------------------------------- Frame pacing
TEST(Physics_InterpolationSmoothsHighFrameRates) {
    // A ball falling under gravity, rendered at 144 FPS while physics steps at 60 Hz.
    auto run = [](bool interpolate, std::vector<float>& ys) {
        Scene s;
        Entity ball = s.CreateEntity("Ball");
        ball.Transform().position = { 0, 50, 0 };
        ball.Add<SphereColliderComponent>().radius = 0.25f;
        auto& rb = ball.Add<RigidBodyComponent>();
        rb.drag = 0.0f;
        rb.interpolate = interpolate;
        s.OnRuntimeStart();
        for (int i = 0; i < 144; i++) {
            s.OnUpdateRuntime(1.0f / 144.0f);
            ys.push_back(ball.Transform().position.y);
        }
        s.OnRuntimeStop();
    };
    std::vector<float> smooth, raw;
    run(true, smooth);
    run(false, raw);
    int frozenSmooth = 0, frozenRaw = 0;
    for (size_t i = 20; i < smooth.size(); i++) {
        if (std::abs(smooth[i] - smooth[i - 1]) < 1e-6f) frozenSmooth++;
        if (std::abs(raw[i] - raw[i - 1]) < 1e-6f) frozenRaw++;
    }
    CHECK(frozenRaw > 30);         // without interpolation over half the frames don't move...
    CHECK(frozenSmooth == 0);      // ...with it, every frame moves
    CHECK(smooth.back() < smooth[20]);   // and it still falls
    // Physics itself is unchanged: after a whole second both are within one step's motion
    CHECK_NEAR(smooth.back(), raw.back(), 0.4f);

    // A script teleporting the body must win over interpolation
    Scene s;
    Entity ball = s.CreateEntity("Ball");
    ball.Transform().position = { 0, 50, 0 };
    ball.Add<SphereColliderComponent>().radius = 0.25f;
    ball.Add<RigidBodyComponent>();
    s.OnRuntimeStart();
    for (int i = 0; i < 10; i++) s.OnUpdateRuntime(1.0f / 144.0f);
    ball.Transform().position = { 100, 5, 0 };
    s.OnUpdateRuntime(1.0f / 144.0f);
    CHECK(std::abs(ball.Transform().position.x - 100.0f) < 1e-4f);
    s.OnRuntimeStop();
}

// ---------------------------------------------------------------------- Events
TEST(Events_EmitOnInvoke) {
    EventSystem ev;
    std::vector<std::string> got;
    auto id = ev.On("Ping", [&](const Event& e) { got.push_back("ping:" + e.data.value("n", std::string())); });
    ev.On("*", [&](const Event& e) { got.push_back("any:" + e.name); });
    ev.Emit("Ping", { { "n", "1" } });
    CHECK(got.empty());                       // queued, not delivered immediately
    ev.Update(0.0f);
    CHECK(got.size() == 2 && got[0] == "ping:1" && got[1] == "any:Ping");
    CHECK(ev.Log().size() == 1 && ev.Log().back().listeners == 2);

    // Listeners can emit; chains are delivered in the same Update
    ev.On("A", [&](const Event&) { ev.Emit("B"); });
    int b = 0;
    ev.On("B", [&](const Event&) { b++; });
    ev.Emit("A");
    ev.Update(0.0f);
    CHECK(b == 1);

    // Off
    got.clear();
    ev.Off(id);
    ev.Emit("Ping");
    ev.Update(0.0f);
    CHECK(got.size() == 1 && got[0] == "any:Ping");

    // Timers
    int once = 0, rep = 0;
    ev.Invoke(1.0f, [&] { once++; });
    auto r = ev.InvokeRepeating(0.5f, 0.5f, [&] { rep++; });
    ev.Update(0.4f); CHECK(once == 0 && rep == 0);
    ev.Update(0.2f); CHECK(once == 0 && rep == 1);   // t = 0.6
    ev.Update(0.5f); CHECK(once == 1 && rep == 2);   // t = 1.1
    ev.Cancel(r);
    ev.Update(2.0f); CHECK(once == 1 && rep == 2);

    // OffAll removes an owner's listeners and timers
    int owned = 0;
    ev.On("X", [&](const Event&) { owned++; }, UUID(42));
    ev.Invoke(0.1f, [&] { owned += 10; }, UUID(42));
    ev.OffAll(UUID(42));
    ev.Emit("X");
    ev.Update(1.0f);
    CHECK(owned == 0);
}

TEST(Events_TriggerOpensGate) {
    // The 3D Starter's setup in miniature: a ball tagged Player falls onto a trigger pad,
    // which emits OpenGate; the gate listens and moves down 3 m over half a second.
    Scene s;
    Entity pad = s.CreateEntity("Pad");
    auto& c = pad.Add<BoxColliderComponent>();
    c.size = vec3(2.0f);
    c.isTrigger = true;
    EventRule step;
    step.when = TriggerWhen::TriggerEnter;
    step.tagFilter = "Player";
    step.action = TriggerDo::EmitEvent;
    step.param = "OpenGate";
    step.once = true;
    pad.Add<EventTriggerComponent>().rules = { step };

    Entity gate = s.CreateEntity("Gate");
    gate.Transform().position = { 10, 1, 0 };
    EventRule open;
    open.when = TriggerWhen::Event;
    open.whenName = "OpenGate";
    open.action = TriggerDo::Move;
    open.vector = { 0, -3, 0 };
    open.duration = 0.5f;
    gate.Add<EventTriggerComponent>().rules = { open };

    Entity decoy = s.CreateEntity("Rock");    // wrong tag: must not open the gate
    decoy.Transform().position = { 0, 2, 0 };
    decoy.Add<SphereColliderComponent>().radius = 0.25f;
    decoy.Add<RigidBodyComponent>();

    // Round trip through the serializer, as play mode does
    json j = SceneSerializer(s).ToJson();
    Scene play;
    SceneSerializer(play).FromJson(j);
    CHECK(play.FindByName("Gate").Get<EventTriggerComponent>().rules.size() == 1);
    CHECK(play.FindByName("Pad").Get<EventTriggerComponent>().rules[0].tagFilter == "Player");
    play.OnRuntimeStart();
    for (int i = 0; i < 60; i++) play.OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(play.FindByName("Gate").Transform().position.y, 1.0f, 1e-4f);   // the rock didn't count

    Entity ball = play.CreateEntity("Ball");
    ball.Get<TagComponent>().tag = "Player";
    ball.Transform().position = { 0, 3, 0 };
    ball.Add<SphereColliderComponent>().radius = 0.25f;
    ball.Add<RigidBodyComponent>();
    for (int i = 0; i < 120; i++) play.OnUpdateRuntime(1.0f / 60.0f);
    CHECK_NEAR(play.FindByName("Gate").Transform().position.y, -2.0f, 1e-3f);
    bool logged = false;
    for (auto& e : play.Events().Log()) logged |= e.name == "OpenGate" && e.listeners == 1;
    CHECK(logged);
    play.OnRuntimeStop();
}

// ---------------------------------------------------------------------- Screen fades & doors
static void StepFade(float seconds) {
    for (float t = 0.0f; t < seconds - 1e-4f; t += 1.0f / 60.0f) ScreenFade::Update(1.0f / 60.0f);
}

TEST(ScreenFade_TransitionRunsCallbackWhileBlack) {
    ScreenFade::Reset();
    int calls = 0;
    float alphaAtCall = -1.0f;
    ScreenFade::Transition(0.5f, 1.0f, 0.5f, [&] { calls++; alphaAtCall = ScreenFade::Alpha(); });
    CHECK(ScreenFade::Busy());
    StepFade(0.4f);
    CHECK(calls == 0);
    CHECK(ScreenFade::Alpha() > 0.7f && ScreenFade::Alpha() < 0.9f);
    StepFade(0.15f);
    CHECK(calls == 1);                       // exactly once, the moment the screen is black
    CHECK_NEAR(alphaAtCall, 1.0f, 1e-5f);
    StepFade(0.8f);
    CHECK_NEAR(ScreenFade::Alpha(), 1.0f, 1e-5f);   // holding on black
    StepFade(0.8f);
    CHECK(ScreenFade::Alpha() == 0.0f);
    CHECK(!ScreenFade::Busy());
    CHECK(calls == 1);

    // A long loading frame must not jump through the fade
    ScreenFade::Transition(0.3f, 0.5f, 0.3f, nullptr);
    ScreenFade::Update(5.0f);
    CHECK(ScreenFade::Alpha() < 0.2f);

    // FadeOut alone stays black until FadeIn
    ScreenFade::Reset();
    ScreenFade::FadeOut(0.2f);
    StepFade(1.0f);
    CHECK_NEAR(ScreenFade::Alpha(), 1.0f, 1e-5f);
    CHECK(ScreenFade::Busy());
    ScreenFade::FadeIn(0.2f);
    StepFade(0.5f);
    CHECK(!ScreenFade::Busy());
}

// A small level: floor, player, a spawn point and some scripted entities.
struct DoorWorld {
    Ref<Scene> scene = MakeRef<Scene>();
    Entity player;
    DoorWorld() {
        Scene& s = *scene;
        Entity floor = s.CreateEntity("Floor");
        floor.Transform().position = { 0, -0.5f, 0 };
        floor.Transform().scale = { 80, 1, 80 };
        floor.Add<BoxColliderComponent>();
        player = s.CreateEntity("Player");
        player.Get<TagComponent>().tag = "Player";
        auto& rb = player.Add<RigidBodyComponent>();
        rb.friction = 0.0f;
        auto& col = player.Add<SphereColliderComponent>();
        col.radius = 0.45f;
        col.offset = { 0, 0.45f, 0 };
    }
    Entity Scripted(const std::string& name, const vec3& pos, const std::string& cls, json fields) {
        Entity e = scene->CreateEntity(name);
        e.Transform().position = pos;
        ScriptInstance si;
        si.className = cls;
        si.fields = std::move(fields);
        e.Add<ScriptComponent>().scripts.push_back(si);
        return e;
    }
    void Start() {
        SaveGame::Clear();
        ScreenFade::Reset();
        SceneManager::SetActive(scene);
        scene->OnRuntimeStart();
    }
    void Run(float seconds) {
        for (float t = 0.0f; t < seconds - 1e-4f; t += 1.0f / 60.0f) {
            scene->OnUpdateRuntime(1.0f / 60.0f);
            ScreenFade::Update(1.0f / 60.0f);
        }
    }
    void Stop() {
        scene->OnRuntimeStop();
        SceneManager::SetActive(nullptr);
        Scene::EndPlaySession();
    }
};

TEST(Doors_SameSceneDoorFadesThenMovesPlayer) {
    DoorWorld w;
    Entity spawn = w.scene->CreateEntity("Spawn - Roof");
    spawn.Transform().position = { 20, 0.2f, 5 };
    spawn.Transform().SetEulerDegrees({ 0, 90, 0 });   // facing -X
    Entity door = w.Scripted("Door", { 0, 1.2f, -1 }, "Door", { { "Spawn Point", "Spawn - Roof" }, { "Open Sound", "" } });
    w.Start();
    w.Run(0.2f);
    auto* d = ScriptableEntity::GetScriptOn<Door>(door);
    CHECK(d != nullptr);
    if (!d) { w.Stop(); return; }
    d->Interact(w.player);
    CHECK(ScreenFade::Busy());
    w.Run(0.25f);
    CHECK(glm::length(vec2(w.player.Transform().position.x, w.player.Transform().position.z)) < 0.5f);   // not yet: still fading out
    w.Run(0.5f);
    vec3 p = w.player.Transform().position;
    CHECK_NEAR(p.x, 20.0f, 0.05f);           // moved while the screen was black
    CHECK_NEAR(p.z, 5.0f, 0.05f);
    vec3 fwd = w.player.Transform().Forward();
    CHECK(fwd.x < -0.99f);                   // facing the way the spawn point faces
    CHECK(ScreenFade::Busy());
    w.Run(2.5f);
    CHECK(!ScreenFade::Busy());
    w.Stop();
}

TEST(Doors_LockedDoorOpensWithKeyItem) {
    DoorWorld w;
    Entity spawn = w.scene->CreateEntity("Spawn - Inside");
    spawn.Transform().position = { -10, 0.2f, 0 };
    Entity door = w.Scripted("Door", { 0, 1.2f, -1 }, "Door",
                             { { "Spawn Point", "Spawn - Inside" }, { "Locked", true }, { "Key Item", "Brass Key" }, { "Open Sound", "" } });
    Entity key = w.Scripted("Key", { 2, 1, 0 }, "Examine", { { "Give Item", "Brass Key" }, { "Hide When Taken", true } });
    w.Start();
    w.Run(0.1f);
    door.Get<ScriptComponent>().scripts[0].instance->OnMessage("Interact");   // same path Event Triggers use
    w.Run(0.1f);
    CHECK(!ScreenFade::Busy());             // locked: nothing happens
    CHECK(ScriptableEntity::GetScriptOn<Door>(door)->locked);
    CHECK(!Inventory::Has("Brass Key"));
    key.Get<ScriptComponent>().scripts[0].instance->OnMessage("Interact");
    CHECK(Inventory::Has("Brass Key"));
    CHECK(!key.IsActive());                 // picked up
    door.Get<ScriptComponent>().scripts[0].instance->OnMessage("Interact");
    CHECK(!ScriptableEntity::GetScriptOn<Door>(door)->locked);   // first use unlocks...
    CHECK(!ScreenFade::Busy());
    door.Get<ScriptComponent>().scripts[0].instance->OnMessage("Interact");
    CHECK(ScreenFade::Busy());              // ...the next one goes through
    w.Run(1.0f);
    CHECK_NEAR(w.player.Transform().position.x, -10.0f, 0.05f);
    w.Stop();
    CHECK(!Inventory::Has("Brass Key"));    // the play session's items end with it
}

TEST(Interaction_PicksWhatThePlayerFaces) {
    DoorWorld w;
    w.Scripted("Front", { 0, 1, -1.2f }, "Examine", { { "Label", "Front" } });
    w.Scripted("Back", { 0, 1, 1.0f }, "Examine", { { "Label", "Back" } });
    w.Scripted("Far", { 6, 1, 0 }, "Examine", { { "Label", "Far" } });
    w.Start();
    w.Run(0.05f);
    Interactable* a = Interactable::FindBest(*w.scene, { 0, 0, 0 }, { 0, 0, -1 });
    Interactable* b = Interactable::FindBest(*w.scene, { 0, 0, 0 }, { 0, 0, 1 });
    Interactable* c = Interactable::FindBest(*w.scene, { 0, 0, 0 }, { 1, 0, 0 });
    CHECK(a && a->label == "Front");
    CHECK(b && b->label == "Back");
    CHECK(c == nullptr);                    // "Far" is out of reach, the others are beside us
    w.Stop();
}

TEST(Physics_PlayerSphereStepsOntoCurb) {
    // The city's sidewalks are 10 cm slabs: a walking player must ride up onto them.
    DoorWorld w;
    Entity curb = w.scene->CreateEntity("Sidewalk");
    curb.Transform().position = { 12, 0.05f, 0 };   // starts at x = 2
    curb.Transform().scale = { 20, 0.1f, 6 };
    curb.Add<BoxColliderComponent>();
    w.Start();
    for (int i = 0; i < 180; i++) {
        w.player.Get<RigidBodyComponent>().velocity.x = 3.5f;
        w.scene->OnUpdateRuntime(1.0f / 60.0f);
    }
    CHECK(w.player.Transform().position.x > 4.0f);
    CHECK(w.player.Transform().position.y > 0.08f);
    w.Stop();
}

// ---------------------------------------------------------------------- Combat
TEST(Combat_HitStopFreezesGameTime) {
    Time::ClearHitStop();
    Time::Advance(0.016f);
    CHECK_NEAR(Time::Delta(), 0.016f, 1e-6f);
    Time::HitStop(0.05f);
    Time::Advance(0.016f);
    CHECK(Time::Delta() == 0.0f);                 // the game stands still...
    CHECK_NEAR(Time::UnscaledDelta(), 0.016f, 1e-6f);   // ...real time doesn't
    for (int i = 0; i < 3; i++) Time::Advance(0.016f);
    Time::Advance(0.016f);
    CHECK_NEAR(Time::Delta(), 0.016f, 1e-6f);     // and it's over after 50 ms
    Time::HitStop(0.1f, 0.25f);                   // slow motion instead of a freeze
    Time::Advance(0.02f);
    CHECK_NEAR(Time::Delta(), 0.005f, 1e-6f);
    Time::HitStop(0.01f);                         // a shorter stop doesn't cut a longer one short
    Time::Advance(0.02f);
    CHECK_NEAR(Time::Delta(), 0.005f, 1e-6f);
    Time::ClearHitStop();
}

TEST(Combat_CameraShakeDecays) {
    CameraShake::Reset();
    mat4 still(1.0f);
    CameraShake::Apply(still);
    CHECK(still == mat4(1.0f));
    CameraShake::Add(0.6f);
    CameraShake::Update(0.01f);
    mat4 shaken(1.0f);
    CameraShake::Apply(shaken);
    CHECK(shaken != mat4(1.0f));
    CameraShake::Add(5.0f);
    CHECK(CameraShake::Trauma() <= 1.0f);
    for (int i = 0; i < 120; i++) CameraShake::Update(1.0f / 60.0f);
    CHECK(CameraShake::Trauma() == 0.0f);
}

TEST(Combat_StyleRewardsVariety) {
    combat::StyleMeter same, varied;
    const char* moves[] = { "Slash", "Chop", "Launcher", "Air Slash", "Plunge", "Lunge" };
    for (const char* m : moves) { same.Add("Slash", 30.0f); varied.Add(m, 30.0f); }
    CHECK(varied.Points() > same.Points() * 1.8f);   // repeating one move is worth much less
    CHECK(varied.Rank() > same.Rank());
    int r = varied.Rank();
    varied.Hurt();
    CHECK(varied.Rank() == r - 1);                    // getting hit costs a whole rank
    for (int i = 0; i < 60 * 30; i++) varied.Update(1.0f / 60.0f);
    CHECK(varied.Rank() == -1);                       // and it drains away when idle
}

TEST(Combat_AttackTokensLimitAttackers) {
    Scene s;
    uint64_t a = s.CreateEntity("A").GetUUID(), b = s.CreateEntity("B").GetUUID(), c = s.CreateEntity("C").GetUUID();
    CHECK(combat::RequestToken(s, a));
    CHECK(combat::RequestToken(s, b));
    CHECK(!combat::RequestToken(s, c));               // two attackers at a time
    CHECK(combat::RequestToken(s, a));                // a holder keeps its token
    combat::ReleaseToken(s, a);
    CHECK(combat::RequestToken(s, c));
    s.DestroyEntity(s.FindByUUID(b));
    CHECK(combat::RequestToken(s, a));                // destroyed holders give theirs back
}

static PlayerCombat* AddFighter(DoorWorld& w) {
    auto& sc = w.player.Add<ScriptComponent>();
    for (const char* cls : { "PlayerController", "PlayerCombat" }) {
        ScriptInstance si;
        si.className = cls;
        sc.scripts.push_back(si);
    }
    return nullptr;
}

TEST(Combat_GroundStringAndPauseCombo) {
    DoorWorld w;
    AddFighter(w);
    w.Start();
    w.Run(0.2f);
    auto* pc = ScriptableEntity::GetScriptOn<PlayerCombat>(w.player);
    CHECK(pc != nullptr);
    if (!pc) { w.Stop(); return; }
    CHECK(pc->CurrentAction().empty());
    // Mashing Attack: slash, backslash, chop, then the spinning finisher
    const char* expected[] = { "Slash", "Backslash", "Chop", "Whirlwind" };
    for (const char* name : expected) {
        pc->Press(PlayerCombat::Command::Attack);
        w.Run(1.0f / 60.0f);
        if (pc->CurrentAction() != name) w.Run(0.3f);   // a queued press waits for the cancel point
        CHECK(pc->CurrentAction() == name);
    }
    w.Run(1.2f);
    CHECK(pc->CurrentAction().empty());
    // Attack, Attack, pause, Attack: the flurry instead of the chop
    pc->Press(PlayerCombat::Command::Attack);
    w.Run(0.25f);
    pc->Press(PlayerCombat::Command::Attack);
    w.Run(0.4f);                                       // backslash plays out...
    w.Run(0.45f);                                      // ...and we wait
    CHECK(pc->CurrentAction().empty());
    pc->Press(PlayerCombat::Command::Attack);
    w.Run(1.0f / 60.0f);
    CHECK(pc->CurrentAction() == "Flurry");
    w.Run(1.0f);
    // Dodge rolls with invincibility frames
    pc->Press(PlayerCombat::Command::Dodge);
    w.Run(0.1f);
    CHECK(pc->CurrentAction() == "Dodge");
    CHECK(pc->Invulnerable());
    HitInfo h;
    h.damage = 30.0f;
    CHECK(!pc->ReceiveHit(h));                         // rolled right through it
    w.Run(0.6f);
    CHECK(!pc->Invulnerable());
    CHECK(pc->ReceiveHit(h));
    CHECK(pc->CurrentAction() == "Hurt");
    w.Stop();
}

TEST(Combat_PuppetStaggersLaunchesAndDies) {
    DoorWorld w;
    Entity puppet = characters::AddPuppet(*w.scene, { 0, 0.1f, -2.0f }, false);
    int defeated = 0;
    w.Start();
    w.scene->Events().On("EnemyDefeated", [&](const Event&) { defeated++; });
    w.Run(1.0f);                                       // rises out of the ground
    auto* c = ScriptableEntity::GetScriptOn<Combatant>(puppet);
    CHECK(c != nullptr);
    if (!c) { w.Stop(); return; }
    std::set<uint64_t> swing;
    HitInfo h;
    h.damage = 10.0f;
    h.from = { 0, 0, 0 };
    vec3 at = w.scene->WorldPosition(puppet) + vec3(0, 1.0f, 0);
    CHECK(combat::Sweep(*w.scene, Team::Player, at, 1.0f, h, swing).size() == 1);
    CHECK_NEAR(c->HealthFraction(), 80.0f / 90.0f, 1e-3f);
    CHECK(combat::Sweep(*w.scene, Team::Player, at, 1.0f, h, swing).empty());   // one hit per swing
    std::set<uint64_t> friendly;
    CHECK(combat::Sweep(*w.scene, Team::Enemy, at, 1.0f, h, friendly).empty());  // no friendly fire

    swing.clear();
    h.launch = 11.0f;
    combat::Sweep(*w.scene, Team::Player, w.scene->WorldPosition(puppet) + vec3(0, 1.0f, 0), 1.0f, h, swing);
    CHECK(c->Airborne());
    w.Run(0.3f);
    CHECK(puppet.Transform().position.y > 1.5f);       // launched
    w.Run(3.0f);
    CHECK(!c->Airborne());                              // landed (and got back up)

    swing.clear();
    h.launch = 0.0f;
    h.damage = 500.0f;
    combat::Sweep(*w.scene, Team::Player, w.scene->WorldPosition(puppet) + vec3(0, 1.0f, 0), 1.5f, h, swing);
    CHECK(c->IsDead());
    w.Run(0.1f);
    CHECK(defeated == 1);
    uint64_t id = puppet.GetUUID();
    w.Run(2.0f);
    CHECK(!w.scene->FindByUUID(id));                    // bursts apart and is removed
    w.Stop();
}

TEST(Combat_BattleZoneRunsWavesThenOpens) {
    DoorWorld w;
    Scene& s = *w.scene;
    Entity seal = s.CreateEntity("Seal");
    seal.Get<TagComponent>().tag = "Seal";
    for (float x : { -3.0f, 0.0f, 3.0f }) {
        Entity p = s.CreateEntity("Spawn");
        p.Transform().position = { x, 0.2f, -6.0f };
        p.Get<TagComponent>().tag = "EnemySpawn";
    }
    Entity templ = characters::AddPuppet(s, { 0, 0.2f, -30.0f }, false);
    templ.Get<TagComponent>().active = false;
    Entity zone = w.Scripted("Zone", { 0, 1, 0 }, "BattleZone",
                             { { "Waves", "2,1" }, { "Heavies", "0,0" }, { "Enemy Template", (uint64_t)templ.GetUUID() }, { "Music", "" } });
    auto& trig = zone.Add<BoxColliderComponent>();
    trig.size = { 4, 4, 4 };
    trig.isTrigger = true;
    auto enemies = [&] { return combat::All(s, Team::Enemy); };
    auto killAll = [&] {
        HitInfo h;
        h.damage = 999.0f;
        for (Combatant* c : enemies()) c->ReceiveHit(h);
    };
    w.Start();
    w.Run(0.2f);
    CHECK(seal.IsActive());                  // the player walked in: sealed
    w.Run(2.5f);
    CHECK(enemies().size() == 2);            // wave 1 (the inactive template doesn't count)
    killAll();
    w.Run(4.0f);
    CHECK(enemies().size() == 1);            // wave 2
    CHECK(seal.IsActive());
    killAll();
    w.Run(4.0f);
    CHECK(enemies().empty());
    CHECK(!seal.IsActive());                 // cleared: the seals drop
    CHECK(SaveGame::GetBool("zone_cleared:" + std::to_string((uint64_t)zone.GetUUID())));
    w.Stop();
}

// ---------------------------------------------------------------------- Asset pipeline
TEST(Assets_TextureFinderFindsWherePacksPutTextures) {
    fs::path root = fs::temp_directory_path() / "euphoria_finder_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    auto touch = [&](const fs::path& p) { fs::create_directories(p.parent_path(), ec); std::ofstream(p) << "x"; };
    touch(root / "Pack/Models/Character.fbx");
    touch(root / "Pack/Textures/Char_Tex.png");
    touch(root / "Pack/Textures/T_Hair_Normal.png");
    touch(root / "Pack/Textures/Wood.png");
    touch(root / "Pack/Textures/Barrel.png");
    touch(root / "Pack/Textures/Barrel_n.png");
    touch(root / "Pack/Textures/Barrel_Emissor.png");
    touch(root / "Pack/Textures/T_Brick_BaseColor.png");
    touch(root / "Pack/Textures/T_Brick_Normal.png");
    touch(root / "Pack/Textures/T_Brick_ORM.png");
    touch(root / "Pack/Textures/T_Hero_Dark.png");
    touch(root / "Pack/Textures/T_Hero_Normal.png");
    TextureFinder::ClearCache();
    fs::path models = root / "Pack/Models";
    auto name = [](const std::string& p) { return fs::path(p).filename().string(); };
    // The artist's own absolute path, in other letter case
    CHECK(name(TextureFinder::Find("C:\\Users\\artist\\Desktop\\CHAR_TEX.png", models)) == "Char_Tex.png");
    // An exporter's "_png.png" doubling; a .tif source with a .png beside it
    CHECK(name(TextureFinder::Find("T_Hair_Normal_png.png", models)) == "T_Hair_Normal.png");
    CHECK(name(TextureFinder::Find("../../sources/Wood.tif", models)) == "Wood.png");
    CHECK(TextureFinder::Find("Nothing.png", models).empty());
    // Companion maps by name
    auto b = TextureFinder::FindCompanions((root / "Pack/Textures/Barrel.png").string());
    CHECK(name(b.normal) == "Barrel_n.png" && name(b.emissive) == "Barrel_Emissor.png");
    auto t = TextureFinder::FindCompanions((root / "Pack/Textures/T_Brick_BaseColor.png").string());
    CHECK(name(t.normal) == "T_Brick_Normal.png" && name(t.orm) == "T_Brick_ORM.png");
    auto h = TextureFinder::FindCompanions((root / "Pack/Textures/T_Hero_Dark.png").string());
    CHECK(name(h.normal) == "T_Hero_Normal.png");
    fs::remove_all(root, ec);
}

TEST(Assets_HumanoidBonesFromAnyNaming) {
    auto rig = [](std::vector<std::pair<const char*, int>> joints) {
        Skeleton s;
        for (auto& [n, p] : joints) { Joint j; j.name = n; j.parent = p; s.joints.push_back(j); }
        return s;
    };
    auto bone = [](const Skeleton& s, HumanBone b) {
        auto h = ClassifyHumanoid(s);
        for (size_t i = 0; i < h.size(); i++) if (h[i] == b) return s.joints[i].name;
        return std::string();
    };
    Skeleton mixamo = rig({ { "mixamorig:Hips", -1 }, { "mixamorig:Spine", 0 }, { "mixamorig:Spine1", 1 }, { "mixamorig:Spine2", 2 },
                            { "mixamorig:Neck", 3 }, { "mixamorig:Head", 4 }, { "mixamorig:HeadTop_End", 5 }, { "mixamorig:LeftShoulder", 3 },
                            { "mixamorig:LeftArm", 7 }, { "mixamorig:LeftForeArm", 8 }, { "mixamorig:LeftHand", 9 }, { "mixamorig:LeftHandIndex1", 10 },
                            { "mixamorig:LeftUpLeg", 0 }, { "mixamorig:LeftLeg", 12 }, { "mixamorig:LeftFoot", 13 }, { "mixamorig:LeftToeBase", 14 },
                            { "mixamorig:RightUpLeg", 0 }, { "mixamorig:RightLeg", 16 } });
    CHECK(bone(mixamo, HumanBone::Hips) == "mixamorig:Hips");
    CHECK(bone(mixamo, HumanBone::UpperChest) == "mixamorig:Spine2");
    CHECK(bone(mixamo, HumanBone::Head) == "mixamorig:Head");                  // not HeadTop_End
    CHECK(bone(mixamo, HumanBone::LeftLowerArm) == "mixamorig:LeftForeArm");   // "forearm" isn't an ear
    CHECK(bone(mixamo, HumanBone::LeftHand) == "mixamorig:LeftHand");          // not the index finger
    CHECK(bone(mixamo, HumanBone::LeftLowerLeg) == "mixamorig:LeftLeg");
    CHECK(bone(mixamo, HumanBone::RightUpperLeg) == "mixamorig:RightUpLeg");
    Skeleton unreal = rig({ { "root", -1 }, { "pelvis", 0 }, { "spine_01", 1 }, { "spine_02", 2 }, { "spine_03", 3 }, { "neck_01", 4 },
                            { "head", 5 }, { "clavicle_l", 4 }, { "upperarm_l", 7 }, { "upperarm_twist_01_l", 8 }, { "lowerarm_l", 8 },
                            { "hand_l", 10 }, { "thigh_r", 1 }, { "calf_r", 12 }, { "foot_r", 13 }, { "ball_r", 14 } });
    CHECK(bone(unreal, HumanBone::Hips) == "pelvis");
    CHECK(bone(unreal, HumanBone::Spine) == "spine_01" && bone(unreal, HumanBone::Chest) == "spine_02");
    CHECK(bone(unreal, HumanBone::LeftUpperArm) == "upperarm_l");             // not the twist bone
    CHECK(bone(unreal, HumanBone::RightToes) == "ball_r");
    Skeleton blender = rig({ { "Hips", -1 }, { "Spine", 0 }, { "Chest", 1 }, { "upper_arm.L", 2 }, { "forearm.L", 3 }, { "hand.L", 4 },
                             { "thigh.R", 0 }, { "shin.R", 6 }, { "foot.R", 7 }, { "DEF-toe.R", 8 } });
    CHECK(bone(blender, HumanBone::LeftUpperArm) == "upper_arm.L");
    CHECK(bone(blender, HumanBone::RightLowerLeg) == "shin.R");
    CHECK(bone(blender, HumanBone::RightToes) == "DEF-toe.R");
    CHECK(NormalizeJointName("mixamorig:LeftForeArm") == "leftforearm");
    CHECK(NormalizeJointName("Armature|DEF-upper_arm.L") == "upper_arm_l");
}

// Two rigs that rest differently: the source in a T-pose with Unreal names, the target with
// Mixamo names, arms hanging down, a different bone roll and twice the size.
static void ArmRigs(Skeleton& src, Skeleton& dst) {
    auto add = [](Skeleton& s, const char* name, int parent, vec3 t, quat r = quat(1, 0, 0, 0)) {
        Joint j; j.name = name; j.parent = parent; j.bindT = t; j.bindR = r; s.joints.push_back(j);
    };
    add(src, "pelvis", -1, { 0, 1, 0 });
    add(src, "spine_01", 0, { 0, 0.2f, 0 });
    add(src, "upperarm_l", 1, { 0.2f, 0.3f, 0 });
    add(src, "lowerarm_l", 2, { 0.3f, 0, 0 });       // T-pose: arm along +X
    add(src, "hand_l", 3, { 0.25f, 0, 0 });
    add(src, "thigh_l", 0, { 0.1f, 0, 0 });
    add(src, "thigh_r", 0, { -0.1f, 0, 0 });
    add(src, "upperarm_r", 1, { -0.2f, 0.3f, 0 });
    quat down = glm::angleAxis(glm::radians(-90.0f), vec3(0, 0, 1));   // turns +X into -Y
    quat roll = glm::angleAxis(glm::radians(40.0f), vec3(1, 0, 0));
    add(dst, "mixamorig:Hips", -1, { 0, 2, 0 });
    add(dst, "mixamorig:Spine", 0, { 0, 0.4f, 0 });
    add(dst, "mixamorig:LeftArm", 1, { 0.4f, 0.6f, 0 }, down * roll);
    add(dst, "mixamorig:LeftForeArm", 2, vec3(glm::inverse(down * roll) * vec3(0, -0.6f, 0)));   // arm hangs down
    add(dst, "mixamorig:LeftHand", 3, vec3(glm::inverse(down * roll) * vec3(0, -0.5f, 0)));
    add(dst, "mixamorig:LeftUpLeg", 0, { 0.2f, 0, 0 });
    add(dst, "mixamorig:RightUpLeg", 0, { -0.2f, 0, 0 });
    add(dst, "mixamorig:RightArm", 1, { -0.4f, 0.6f, 0 });
    for (Skeleton* s : { &src, &dst }) {
        std::vector<mat4> g(s->joints.size());
        for (size_t i = 0; i < g.size(); i++) {
            auto& j = s->joints[i];
            g[i] = (j.parent >= 0 ? g[j.parent] : mat4(1.0f)) * ComposeTRS(j.bindT, j.bindR, j.bindS);
            j.inverseBind = glm::inverse(g[i]);
            j.skinned = true;
        }
    }
}

TEST(Assets_RetargetMovesDifferentRigsAlike) {
    Skeleton src, dst;
    ArmRigs(src, dst);
    // Source clip: over one second the left arm swings from straight out to pointing forward
    // (+Z), and the hips rise 10 cm
    AnimationClip clip;
    clip.name = "Reach";
    clip.duration = 1.0f;
    JointChannel arm;
    arm.joint = 2;
    arm.rotation = { { 0.0f, 1.0f }, { quat(1, 0, 0, 0), glm::angleAxis(glm::radians(-90.0f), vec3(0, 1, 0)) } };
    JointChannel hips;
    hips.joint = 0;
    hips.translation = { { 0.0f, 1.0f }, { vec3(0, 1, 0), vec3(0, 1.1f, 0) } };
    clip.channels = { arm, hips };

    RetargetMap map = MapSkeletons(src, dst);
    CHECK(!map.sameRig);
    CHECK(map.mapped >= 6);
    auto out = RetargetClip(clip, src, dst, map);
    CHECK(out != nullptr);
    if (!out) return;
    auto armDirection = [&](float t) {
        Pose p;
        SampleClip(*out, dst, t, p);
        std::vector<mat4> g(dst.joints.size());
        for (size_t i = 0; i < g.size(); i++)
            g[i] = (dst.joints[i].parent >= 0 ? g[dst.joints[i].parent] : mat4(1.0f)) * ComposeTRS(p[i].t, p[i].r, p[i].s);
        return std::make_pair(glm::normalize(vec3(g[3][3]) - vec3(g[2][3])), vec3(g[0][3]));
    };
    auto [d0, hips0] = armDirection(0.0f);
    auto [d1, hips1] = armDirection(1.0f);
    CHECK(glm::dot(d0, vec3(1, 0, 0)) > 0.95f);    // target arm raised to the source's T-pose
    CHECK(glm::dot(d1, vec3(0, 0, 1)) > 0.95f);    // and swung forward with it
    CHECK_NEAR(hips1.y - hips0.y, 0.2f, 0.02f);    // 10 cm on a rig half the size = 20 cm here

    // The same rig: keys are copied
    Skeleton a, b;
    ArmRigs(a, b);
    RetargetMap same = MapSkeletons(a, a);
    CHECK(same.sameRig);
    auto copy = RetargetClip(clip, a, a, same);
    CHECK(copy && copy->channels.size() == clip.channels.size());
}

TEST(Assets_LocomotionFromAnimationLibraries) {
    // Quaternius' Universal Animation Library
    auto u = FindLocomotionClips({ "A_TPose", "Crouch_Fwd_Loop", "Crouch_Idle_Loop", "Idle_Loop", "Idle_Talking_Loop", "Jog_Fwd_Loop",
                                   "Jump_Land", "Jump_Loop", "Jump_Start", "Pistol_Idle_Loop", "Sprint_Loop", "Swim_Idle_Loop",
                                   "Walk_Formal_Loop", "Walk_Loop" });
    CHECK(u.idle == "Idle_Loop");
    CHECK(u.walk == "Walk_Loop");
    CHECK(u.run == "Jog_Fwd_Loop");
    CHECK(u.fall == "Jump_Loop");
    CHECK(u.jump == "Jump_Start");
    // Mixamo downloads
    auto m = FindLocomotionClips({ "Breathing Idle", "Walking", "Running", "Jumping" });
    CHECK(m.idle == "Breathing Idle" && m.walk == "Walking" && m.run == "Running" && m.jump == "Jumping");
}

TEST(Assets_WeldAndClipNames) {
    std::vector<Vertex> v(6);
    for (int i = 0; i < 6; i++) v[i].position = vec3((float)(i % 3), 0, 0);   // two triangles sharing all three corners
    std::vector<uint32_t> idx = { 0, 1, 2, 3, 4, 5 };
    ModelImport::Weld(v, idx);
    CHECK(v.size() == 3);
    CHECK(idx[3] == 0 && idx[4] == 1 && idx[5] == 2);
    CHECK(ModelImport::CleanClipName("Armature|Run", "Hero.fbx", 4) == "Run");
    CHECK(ModelImport::CleanClipName("mixamo.com", "Models/Sword Slash.fbx", 1) == "Sword Slash");
    CHECK(ModelImport::CleanClipName("Take 001", "Wave.fbx", 1) == "Wave");
}


// ---------------------------------------------------------------------- 2D
TEST(Xml_Parse) {
    XmlNode root;
    std::string err;
    CHECK(ParseXml("<?xml version=\"1.0\"?>\n<!-- c --><map a=\"1\" b='x &amp; y'><tile id=\"3\"/><data>1,2&#44;3<![CDATA[<raw>]]></data></map>", root, &err));
    CHECK(root.name == "map");
    CHECK(root.Int("a") == 1);
    CHECK(root.Str("b") == "x & y");
    CHECK(root.Child("tile") && root.Child("tile")->Int("id") == 3);
    CHECK(root.Child("data") && root.Child("data")->text == "1,2,3<raw>");
    CHECK(!ParseXml("<a>\n<b></a>", root, &err));
    CHECK(err.find("line 2") != std::string::npos);
}

static const char* kTestTmx = R"(<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" orientation="orthogonal" width="4" height="3" tilewidth="16" tileheight="16" infinite="0">
 <tileset firstgid="1" name="t" tilewidth="16" tileheight="16" tilecount="8" columns="4">
  <image source="tiles.png" width="64" height="32"/>
  <tile id="0"><objectgroup><object id="1" x="0" y="0" width="16" height="16"/></objectgroup></tile>
  <tile id="5"><animation><frame tileid="5" duration="100"/><frame tileid="6" duration="300"/></animation></tile>
 </tileset>
 <layer id="1" name="Ground" width="4" height="3">
  <data encoding="csv">
0,0,0,0,
0,6,0,2147483650,
1,1,1,0
</data>
 </layer>
 <group id="5" name="G" opacity="0.5" offsetx="2">
  <layer id="2" name="Walls" width="4" height="3" opacity="0.5">
   <properties><property name="collision" type="bool" value="true"/></properties>
   <data encoding="csv">0,0,0,3,0,0,0,3,0,0,0,0</data>
  </layer>
 </group>
 <objectgroup id="3" name="Things">
  <object id="1" name="Start" type="Spawn" x="8" y="32"><point/></object>
  <object id="2" gid="2" x="16" y="48" width="16" height="16"/>
 </objectgroup>
</map>)";

TEST(Tiled_LoadTmx) {
    std::string err;
    auto map = Tiled::LoadFromText(kTestTmx, false, ".", &err);
    CHECK(map != nullptr);
    if (!map) return;
    CHECK(map->width == 4 && map->height == 3 && map->tileWidth == 16);
    CHECK(map->layers.size() == 3);
    auto& ground = map->layers[0];
    CHECK(ground.At(1, 1) == 6);
    CHECK((ground.At(3, 1) & kTileFlipH) && (ground.At(3, 1) & kTileGidMask) == 2);
    int local = -1;
    const Tileset* ts = map->FindTileset(ground.At(3, 1), local);
    CHECK(ts && local == 1);
    std::string img;
    ivec4 rect;
    CHECK(ts && ts->Source(5, img, rect) && rect == ivec4(16, 16, 16, 16));
    CHECK(ts && ts->Animated(5, 0.05f) == 5 && ts->Animated(5, 0.2f) == 6 && ts->Animated(5, 0.45f) == 5);
    // Group opacity and offsets inherit
    CHECK_NEAR(map->layers[1].opacity, 0.25f, 1e-5f);
    CHECK_NEAR(map->layers[1].offset.x, 2.0f, 1e-5f);
    CHECK(map->layers[1].collision);
    // Draw order: the object layer with a tile object is where characters walk
    CHECK(map->layers[2].ySort && map->layers[2].order == 0 && map->layers[0].order < 0);
    auto* start = map->FindObject("Start");
    CHECK(start && start->shape == TiledShape::Point && start->type == "Spawn");
    // Map pixels -> local units: (8, 32) in a 48px-tall map is 1 unit up, 0.5 right
    vec2 l = map->MapToLocal({ 8, 32 }, 16.0f);
    CHECK_NEAR(l.x, 0.5f, 1e-5f);
    CHECK_NEAR(l.y, 1.0f, 1e-5f);
    // Colliders: the bottom row of tile 1 merges into one box; the wall column (offset 2px) into another
    bool floorFound = false, wallFound = false;
    for (auto& r : map->colliders) {
        if (r.x == 0 && r.y == 32 && r.w == 48 && r.h == 16) floorFound = true;
        if (r.x == 50 && r.y == 0 && r.w == 16 && r.h == 32) wallFound = true;
    }
    CHECK(floorFound);
    CHECK(wallFound);
}

TEST(Tiled_EncodingsAndJson) {
    // The same 8 gids as CSV, base64, base64+zlib and base64+gzip
    const char* tmpl = R"(<map width="4" height="2" tilewidth="16" tileheight="16"><tileset firstgid="1" tilewidth="16" tileheight="16" tilecount="8" columns="4"><image source="t.png" width="64" height="32"/></tileset><layer name="L" width="4" height="2"><data encoding="base64"%s>%s</data></layer></map>)";
    const char* variants[][2] = {
        { "", "AQAAAAIAAAADAAAABAAAgAAAAAAFAAAABgAAAAEAAEA=" },
        { " compression=\"zlib\"", "eJwFwQEBADAEALA5nks00W2Bh0Sx0PgI5gAKQADX" },
        { " compression=\"gzip\"", "H4sIAAAAAAAC/2NkYGBgAmJmIGZhYGgAUgysQMwGxIwMDA4Axnto1yAAAAA=" },
    };
    for (auto& v : variants) {
        char buf[2048];
        std::snprintf(buf, sizeof(buf), tmpl, v[0], v[1]);
        auto map = Tiled::LoadFromText(buf, false, ".");
        CHECK(map && map->layers.size() == 1);
        if (!map || map->layers.empty()) continue;
        auto& g = map->layers[0].gids;
        CHECK(g.size() == 8 && g[0] == 1 && g[2] == 3 && g[3] == (0x80000004u) && g[7] == 0x40000001u);
    }
    // Tiled JSON (.tmj)
    const char* tmj = R"({"width":2,"height":2,"tilewidth":16,"tileheight":16,"infinite":false,
        "tilesets":[{"firstgid":1,"tilewidth":16,"tileheight":16,"tilecount":4,"columns":2,"image":"t.png","imagewidth":32,"imageheight":32,
                     "tiles":[{"id":0,"objectgroup":{"objects":[{"x":0,"y":8,"width":16,"height":8}]}}]}],
        "layers":[{"type":"tilelayer","name":"Ground","width":2,"height":2,"data":[1,1,0,4]},
                  {"type":"objectgroup","name":"Obj","objects":[{"name":"Spawn","x":4,"y":20,"point":true}]}]})";
    auto map = Tiled::LoadFromText(tmj, true, ".");
    CHECK(map && map->layers.size() == 2);
    if (!map) return;
    CHECK(map->layers[0].At(1, 1) == 4);
    CHECK(map->FindObject("Spawn") && map->FindObject("Spawn")->shape == TiledShape::Point);
    CHECK(map->colliders.size() == 1 && map->colliders[0].w == 32 && map->colliders[0].h == 8 && map->colliders[0].y == 8);
}

TEST(Tiled_MergeRects) {
    // A 3x2 block of full tiles becomes one rectangle; a separate tile stays apart
    std::vector<TileRect> r;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++) r.push_back({ x * 16.0f, y * 16.0f, 16, 16 });
    r.push_back({ 80, 0, 16, 16 });
    auto m = Tiled::MergeRects(r);
    CHECK(m.size() == 2);
    bool big = false;
    for (auto& x : m) big |= x.x == 0 && x.y == 0 && x.w == 48 && x.h == 32;
    CHECK(big);
}

TEST(SpriteSheet_DetectFrames) {
    auto sheet = [](int w, int h, auto opaque) {
        std::vector<uint8_t> px((size_t)w * h * 4, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (opaque(x, y)) px[((size_t)y * w + x) * 4 + 3] = 255;
        return px;
    };
    // 6 frames of 24x20 with gaps (a small figure in each)
    auto a = sheet(144, 20, [](int x, int y) { int fx = x % 24; return fx >= 8 && fx < 16 && y >= 4 && y < 18; });
    CHECK(SpriteSheet::DetectFrameSize(a.data(), 144, 20) == ivec2(24, 20));
    // A grid: 4 columns x 3 rows of 32x32
    auto b = sheet(128, 96, [](int x, int y) { return x % 32 >= 10 && x % 32 < 22 && y % 32 >= 6 && y % 32 < 30; });
    CHECK(SpriteSheet::DetectFrameSize(b.data(), 128, 96) == ivec2(32, 32));
    // Touching 16x32 frames: wide in the middle, thin where they meet
    auto c = sheet(96, 32, [](int x, int y) { int fx = x % 16; return y > 4 && (fx >= 2 && fx < 14 ? true : y > 26); });
    CHECK(SpriteSheet::DetectFrameSize(c.data(), 96, 32) == ivec2(16, 32));
    // A fully opaque strip: square frames
    auto d = sheet(64, 16, [](int, int) { return true; });
    CHECK(SpriteSheet::DetectFrameSize(d.data(), 64, 16) == ivec2(16, 16));
    // Frame rectangles for an animation (row-major from the top-left)
    Sprite2D::FrameGrid g;
    g.frameSize = { 16, 16 };
    g.columns = 4;
    g.first = 2;
    g.count = 4;
    CHECK(Sprite2D::FrameRect(g, 3) == vec4(16, 16, 16, 16));   // frame index 5: second row
}

TEST(Physics2D_TilemapFloor) {
    fs::path dir = fs::temp_directory_path() / "euphoria_tilemap_test";
    fs::create_directories(dir);
    { std::ofstream(dir / "floor.tmx") << kTestTmx; }
    Scene scene;
    auto& env = scene.Environment();
    env.mode2D = true;
    env.pixelsPerUnit = 16.0f;
    env.gravity = { 0, -40, 0 };
    Entity map = scene.CreateEntity("Map");
    map.Add<TilemapComponent>().map = (dir / "floor.tmx").string();
    Entity box = scene.CreateEntity("Box");
    box.Transform().position = { 1.0f, 2.5f, 0.3f };
    box.Add<RigidBodyComponent>();
    box.Add<BoxColliderComponent>().size = { 0.5f, 0.5f, 1.0f };
    scene.OnRuntimeStart();
    for (int i = 0; i < 120; i++) scene.OnUpdateRuntime(1.0f / 60.0f);
    auto& t = box.Transform();
    // The floor row (tiles at the bottom of the 3-row map) has its top at y = 1
    CHECK_NEAR(t.position.y, 1.25f, 0.03f);
    CHECK_NEAR(t.position.z, 0.3f, 1e-4f);   // 2D: stays in its plane
    CHECK(box.Get<RigidBodyComponent>().grounded);
    scene.OnRuntimeStop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(Scene2D_SerializeAndView) {
    Scene a;
    a.Environment().mode2D = true;
    a.Environment().pixelsPerUnit = 32.0f;
    a.Environment().referenceResolution = { 480, 270 };
    Entity e = a.CreateEntity("Hero");
    auto& sr = e.Add<SpriteRendererComponent>();
    sr.texture = "Art/hero.png";
    sr.region = { 16, 0, 16, 24 };
    sr.pivot = { 0.5f, 0.0f };
    sr.flipX = true;
    sr.order = 3;
    auto& an = e.Add<SpriteAnimatorComponent>();
    an.animations.push_back(Sprite2D::FromSheet("missing.png", "walk_down", 8.0f));
    an.animations[0].firstFrame = 4;
    an.animations[0].loop = false;
    e.Add<TilemapComponent>().map = "Maps/Level.tmx";
    json j = SceneSerializer(a).ToJson();
    Scene b;
    SceneSerializer(b).FromJson(j);
    CHECK(b.Environment().mode2D && b.Environment().pixelsPerUnit == 32.0f && b.Environment().referenceResolution == ivec2(480, 270));
    Entity h = b.FindByName("Hero");
    CHECK(h && h.Has<SpriteRendererComponent>() && h.Has<SpriteAnimatorComponent>() && h.Has<TilemapComponent>());
    if (h) {
        auto& s2 = h.Get<SpriteRendererComponent>();
        CHECK(s2.texture == "Art/hero.png" && s2.region == vec4(16, 0, 16, 24) && s2.pivot == vec2(0.5f, 0.0f) && s2.flipX && s2.order == 3);
        auto& a2 = h.Get<SpriteAnimatorComponent>();
        CHECK(a2.animations.size() == 1 && a2.animations[0].name == "walk_down" && a2.animations[0].firstFrame == 4 && !a2.animations[0].loop);
        CHECK(h.Get<TilemapComponent>().map == "Maps/Level.tmx");
    }
    // Pixel-perfect view: whole-number scale, low resolution covering the window
    SceneEnvironment env;
    env.pixelsPerUnit = 16.0f;
    env.referenceResolution = { 320, 180 };
    RenderCamera cam;
    cam.view = glm::translate(mat4(1.0f), -vec3(10.37f, 4.12f, 10.0f));
    cam.projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    auto v = Renderer2D::ComputeView(env, cam, 1280, 720, false);
    CHECK(v.lowRes && v.scale == 4 && v.lowWidth == 320 && v.lowHeight == 180);
    auto v2 = Renderer2D::ComputeView(env, cam, 1366, 768, false);
    CHECK(v2.scale == 4 && v2.lowWidth == 342 && v2.lowHeight == 192);
    // The view's left edge sits on the art's pixel grid
    mat4 inv = glm::inverse(v.camera.projection * v.camera.view);
    vec4 left = inv * vec4(-1, 0, 0, 1);
    float px = left.x / left.w * 16.0f;
    CHECK_NEAR(px, std::round(px), 1e-3f);
    // Editor view: unchanged
    CHECK(!Renderer2D::ComputeView(env, cam, 1280, 720, true).lowRes);
}

int main() {
    std::printf("Euphoria engine tests\n");
    for (auto& t : Tests()) {
        int before = g_Failures;
        t.fn();
        std::printf("  [%s] %s\n", g_Failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("%d checks, %d failure(s)\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
