// Headless unit tests for engine systems (no window or GPU needed).
// Run:  ctest --output-on-failure    or    ./9divine-tests
#include <nd/9divine.h>
#include <nd/scene/SceneSerializer.h>
#include <nd/core/Json.h>
#include <cstdio>
#include <cmath>

using namespace nd;

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

int main() {
    std::printf("9divine engine tests\n");
    for (auto& t : Tests()) {
        int before = g_Failures;
        t.fn();
        std::printf("  [%s] %s\n", g_Failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("%d checks, %d failure(s)\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
