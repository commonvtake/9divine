#include "nd/ai/Navigation.h"
#include "nd/scene/Scene.h"
#include "nd/scene/Entity.h"
#include "nd/physics/Physics.h"
#include "nd/renderer/DebugDraw.h"
#include "nd/core/Profiler.h"
#include "nd/core/Log.h"
#include <queue>
#include <cmath>

namespace nd {

void NavGrid::Bake() {
    ND_PROFILE_SCOPE("NavBake");
    Registry& reg = m_Scene->Reg();
    Settings& s = settings;

    // Bounds
    if (s.autoBounds) {
        AABB b;
        reg.View<TransformComponent, TerrainComponent>([&](EntityId id, TransformComponent&, TerrainComponent& t) {
            vec3 p = m_Scene->WorldPosition(Entity(id, m_Scene));
            float h = t.size * 0.5f;
            b.Expand(p + vec3(-h, -5.0f, -h));
            b.Expand(p + vec3(h, t.heightScale + 10.0f, h));
        });
        reg.View<TransformComponent, BoxColliderComponent>([&](EntityId id, TransformComponent&, BoxColliderComponent& c) {
            vec3 p = m_Scene->WorldPosition(Entity(id, m_Scene));
            b.Expand(p - c.size * 2.0f);
            b.Expand(p + c.size * 2.0f);
        });
        if (b.Valid()) { s.boundsMin = b.min; s.boundsMax = b.max; }
    }
    m_Origin = s.boundsMin;
    m_W = std::max(1, (int)std::ceil((s.boundsMax.x - s.boundsMin.x) / s.cellSize));
    m_D = std::max(1, (int)std::ceil((s.boundsMax.z - s.boundsMin.z) / s.cellSize));
    if ((size_t)m_W * m_D > 4'000'000) {
        ND_WARN("Nav grid too large ({}x{}); increase cell size", m_W, m_D);
        m_Cells.clear();
        return;
    }
    m_Cells.assign((size_t)m_W * m_D, Cell{});
    PhysicsWorld& phys = m_Scene->Physics();
    float cosSlope = std::cos(glm::radians(s.maxSlope));
    float top = s.boundsMax.y + 1.0f;
    phys.BeginBatchQueries();

    for (int z = 0; z < m_D; z++) {
        for (int x = 0; x < m_W; x++) {
            vec3 c = CellCenter(x, z);
            Cell& cell = m_Cells[z * m_W + x];
            // Highest surface under the cell: ray straight down (colliders + terrain).
            RaycastHit hit;
            Ray ray{ vec3(c.x, top, c.z), vec3(0, -1, 0) };
            if (!phys.Raycast(ray, top - s.boundsMin.y + 2.0f, hit)) continue;
            // Moving bodies aren't part of the static world.
            if (hit.entity) {
                auto* rb = hit.entity.TryGet<RigidBodyComponent>();
                if (rb && rb->type != BodyType::Static) {
                    // look under the dynamic body
                    Ray r2{ hit.point - vec3(0, 0.05f, 0), vec3(0, -1, 0) };
                    RaycastHit h2;
                    if (!phys.Raycast(r2, 200.0f, h2, hit.entity.Id())) continue;
                    hit = h2;
                }
            }
            if (hit.normal.y < cosSlope) continue;
            if (hit.point.y < s.minWalkableHeight) continue;
            cell.height = hit.point.y;
            cell.walkable = true;
            // Clearance: anything blocking the agent's body?
            float r = s.agentRadius;
            vec3 probe = hit.point + vec3(0, r + s.maxStep, 0);
            for (auto& e : phys.OverlapSphere(probe, r, false)) {
                auto* rb = e.TryGet<RigidBodyComponent>();
                if (!rb || rb->type == BodyType::Static) { cell.walkable = false; break; }
            }
        }
    }
    phys.EndBatchQueries();
    ND_INFO("Nav grid baked: {}x{} cells", m_W, m_D);
}

vec3 NavGrid::CellCenter(int x, int z) const {
    return { m_Origin.x + (x + 0.5f) * settings.cellSize, 0.0f, m_Origin.z + (z + 0.5f) * settings.cellSize };
}

bool NavGrid::ToCell(const vec3& p, int& x, int& z) const {
    x = (int)std::floor((p.x - m_Origin.x) / settings.cellSize);
    z = (int)std::floor((p.z - m_Origin.z) / settings.cellSize);
    return x >= 0 && z >= 0 && x < m_W && z < m_D;
}

bool NavGrid::IsWalkable(const vec3& p) const {
    int x, z;
    return ToCell(p, x, z) && m_Cells[z * m_W + x].walkable;
}

bool NavGrid::SampleHeight(const vec3& p, float& h) const {
    int x, z;
    if (!ToCell(p, x, z) || !m_Cells[z * m_W + x].walkable) return false;
    h = m_Cells[z * m_W + x].height;
    return true;
}

vec3 NavGrid::ClosestWalkable(const vec3& p) const {
    int cx, cz;
    ToCell(p, cx, cz);
    cx = glm::clamp(cx, 0, m_W - 1);
    cz = glm::clamp(cz, 0, m_D - 1);
    for (int r = 0; r < 64; r++) {
        float best = 1e30f;
        vec3 bestP = p;
        for (int z = cz - r; z <= cz + r; z++)
            for (int x = cx - r; x <= cx + r; x++) {
                if (std::max(std::abs(x - cx), std::abs(z - cz)) != r) continue;
                const Cell* c = At(x, z);
                if (!c || !c->walkable) continue;
                vec3 cc = CellCenter(x, z);
                cc.y = c->height;
                float d = glm::distance(vec2(cc.x, cc.z), vec2(p.x, p.z));
                if (d < best) { best = d; bestP = cc; }
            }
        if (best < 1e29f) return bestP;
    }
    return p;
}

vec3 NavGrid::RandomPoint(uint32_t& seed) const {
    for (int i = 0; i < 200 && !m_Cells.empty(); i++) {
        seed = seed * 1664525u + 1013904223u;
        int idx = (int)(seed % (uint32_t)m_Cells.size());
        if (m_Cells[idx].walkable) {
            vec3 c = CellCenter(idx % m_W, idx / m_W);
            c.y = m_Cells[idx].height;
            return c;
        }
    }
    return vec3(0.0f);
}

bool NavGrid::LineOfSight(int x0, int z0, int x1, int z1) const {
    int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0);
    int sx = x0 < x1 ? 1 : -1, sz = z0 < z1 ? 1 : -1;
    int err = dx - dz;
    float prevH = At(x0, z0) ? At(x0, z0)->height : 0.0f;
    while (true) {
        const Cell* c = At(x0, z0);
        if (!c || !c->walkable) return false;
        if (std::abs(c->height - prevH) > settings.maxStep) return false;
        prevH = c->height;
        if (x0 == x1 && z0 == z1) return true;
        int e2 = 2 * err;
        if (e2 > -dz) { err -= dz; x0 += sx; }
        if (e2 < dx) { err += dx; z0 += sz; }
    }
}

bool NavGrid::FindPath(const vec3& start, const vec3& end, std::vector<vec3>& out) {
    out.clear();
    if (m_Cells.empty()) return false;
    vec3 s = IsWalkable(start) ? start : ClosestWalkable(start);
    vec3 e = IsWalkable(end) ? end : ClosestWalkable(end);
    int sx, sz, ex, ez;
    if (!ToCell(s, sx, sz) || !ToCell(e, ex, ez)) return false;
    if (!At(sx, sz)->walkable || !At(ex, ez)->walkable) return false;

    const int N = m_W * m_D;
    std::vector<float> g(N, 1e30f);
    std::vector<int> parent(N, -1);
    std::vector<uint8_t> closed(N, 0);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    auto h = [&](int x, int z) {
        float dx = (float)std::abs(x - ex), dz = (float)std::abs(z - ez);
        return (dx + dz) + (1.41421356f - 2.0f) * std::min(dx, dz);   // octile
    };
    int startIdx = sz * m_W + sx, goal = ez * m_W + ex;
    g[startIdx] = 0.0f;
    open.push({ h(sx, sz), startIdx });
    const int dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1} };
    int expanded = 0;
    while (!open.empty()) {
        int cur = open.top().second;
        open.pop();
        if (closed[cur]) continue;
        closed[cur] = 1;
        if (cur == goal) break;
        if (++expanded > 200000) break;
        int cx = cur % m_W, cz = cur / m_W;
        float ch = m_Cells[cur].height;
        for (auto& d : dirs) {
            int nx = cx + d[0], nz = cz + d[1];
            const Cell* nc = At(nx, nz);
            if (!nc || !nc->walkable) continue;
            if (std::abs(nc->height - ch) > settings.maxStep) continue;
            if (d[0] != 0 && d[1] != 0) {   // no corner cutting
                const Cell* a = At(cx + d[0], cz);
                const Cell* b = At(cx, cz + d[1]);
                if (!a || !b || !a->walkable || !b->walkable) continue;
            }
            int ni = nz * m_W + nx;
            float cost = g[cur] + ((d[0] != 0 && d[1] != 0) ? 1.41421356f : 1.0f) + std::abs(nc->height - ch);
            if (cost < g[ni]) {
                g[ni] = cost;
                parent[ni] = cur;
                open.push({ cost + h(nx, nz), ni });
            }
        }
    }
    if (parent[goal] < 0 && goal != startIdx) return false;

    std::vector<int> cells;
    for (int c = goal; c != -1; c = parent[c]) cells.push_back(c);
    std::reverse(cells.begin(), cells.end());

    // String pulling
    std::vector<int> smooth;
    size_t i = 0;
    smooth.push_back(cells[0]);
    while (i < cells.size() - 1) {
        size_t j = cells.size() - 1;
        for (; j > i + 1; j--)
            if (LineOfSight(cells[i] % m_W, cells[i] / m_W, cells[j] % m_W, cells[j] / m_W)) break;
        smooth.push_back(cells[j]);
        i = j;
    }
    for (size_t k = 0; k < smooth.size(); k++) {
        int c = smooth[k];
        vec3 p = CellCenter(c % m_W, c / m_W);
        p.y = m_Cells[c].height;
        out.push_back(p);
    }
    if (!out.empty()) out.back() = vec3(e.x, out.back().y, e.z);
    return true;
}

void NavGrid::DebugDraw() const {
    if (m_Cells.empty()) return;
    int step = std::max(1, std::max(m_W, m_D) / 160);
    float half = settings.cellSize * step * 0.4f;
    for (int z = 0; z < m_D; z += step)
        for (int x = 0; x < m_W; x += step) {
            const Cell& c = m_Cells[z * m_W + x];
            if (!c.walkable) continue;
            vec3 p = CellCenter(x, z);
            p.y = c.height + 0.05f;
            DebugDraw::Line(p - vec3(half, 0, 0), p + vec3(half, 0, 0), { 0.2f, 0.6f, 1.0f, 0.6f });
            DebugDraw::Line(p - vec3(0, 0, half), p + vec3(0, 0, half), { 0.2f, 0.6f, 1.0f, 0.6f });
        }
}

// ===================================================================== BT
namespace {
class SequenceNode : public BTNode {
public:
    explicit SequenceNode(std::vector<BTNodeRef> c) : m_Children(std::move(c)) {}
    BTStatus Tick(float dt) override {
        while (m_Index < m_Children.size()) {
            BTStatus s = m_Children[m_Index]->Tick(dt);
            if (s == BTStatus::Running) return s;
            if (s == BTStatus::Failure) { Reset(); return s; }
            m_Index++;
        }
        Reset();
        return BTStatus::Success;
    }
    void Reset() override { m_Index = 0; for (auto& c : m_Children) c->Reset(); }
private:
    std::vector<BTNodeRef> m_Children;
    size_t m_Index = 0;
};

class SelectorNode : public BTNode {
public:
    explicit SelectorNode(std::vector<BTNodeRef> c) : m_Children(std::move(c)) {}
    BTStatus Tick(float dt) override {
        // Re-evaluated from the top each tick, so higher-priority branches can interrupt.
        for (size_t i = 0; i < m_Children.size(); i++) {
            BTStatus s = m_Children[i]->Tick(dt);
            if (s != BTStatus::Failure) {
                if (m_Running != (int)i && m_Running >= 0) m_Children[m_Running]->Reset();
                m_Running = s == BTStatus::Running ? (int)i : -1;
                return s;
            }
        }
        m_Running = -1;
        return BTStatus::Failure;
    }
    void Reset() override { m_Running = -1; for (auto& c : m_Children) c->Reset(); }
private:
    std::vector<BTNodeRef> m_Children;
    int m_Running = -1;
};

class ParallelNode : public BTNode {
public:
    explicit ParallelNode(std::vector<BTNodeRef> c) : m_Children(std::move(c)) {}
    BTStatus Tick(float dt) override {
        bool running = false;
        for (auto& c : m_Children) {
            BTStatus s = c->Tick(dt);
            if (s == BTStatus::Failure) return s;
            if (s == BTStatus::Running) running = true;
        }
        return running ? BTStatus::Running : BTStatus::Success;
    }
    void Reset() override { for (auto& c : m_Children) c->Reset(); }
private:
    std::vector<BTNodeRef> m_Children;
};

class ActionNode : public BTNode {
public:
    explicit ActionNode(std::function<BTStatus(float)> f) : m_Fn(std::move(f)) {}
    BTStatus Tick(float dt) override { return m_Fn(dt); }
private:
    std::function<BTStatus(float)> m_Fn;
};

class ConditionNode : public BTNode {
public:
    explicit ConditionNode(std::function<bool()> f) : m_Fn(std::move(f)) {}
    BTStatus Tick(float) override { return m_Fn() ? BTStatus::Success : BTStatus::Failure; }
private:
    std::function<bool()> m_Fn;
};

class InverterNode : public BTNode {
public:
    explicit InverterNode(BTNodeRef c) : m_Child(std::move(c)) {}
    BTStatus Tick(float dt) override {
        BTStatus s = m_Child->Tick(dt);
        if (s == BTStatus::Running) return s;
        return s == BTStatus::Success ? BTStatus::Failure : BTStatus::Success;
    }
    void Reset() override { m_Child->Reset(); }
private:
    BTNodeRef m_Child;
};

class RepeatNode : public BTNode {
public:
    RepeatNode(BTNodeRef c, int n) : m_Child(std::move(c)), m_Times(n) {}
    BTStatus Tick(float dt) override {
        BTStatus s = m_Child->Tick(dt);
        if (s == BTStatus::Running) return s;
        m_Count++;
        m_Child->Reset();
        if (m_Times > 0 && m_Count >= m_Times) { m_Count = 0; return BTStatus::Success; }
        return BTStatus::Running;
    }
    void Reset() override { m_Count = 0; m_Child->Reset(); }
private:
    BTNodeRef m_Child;
    int m_Times, m_Count = 0;
};

class WaitNode : public BTNode {
public:
    explicit WaitNode(float s) : m_Duration(s) {}
    BTStatus Tick(float dt) override {
        m_Time += dt;
        if (m_Time >= m_Duration) { m_Time = 0.0f; return BTStatus::Success; }
        return BTStatus::Running;
    }
    void Reset() override { m_Time = 0.0f; }
private:
    float m_Duration, m_Time = 0.0f;
};

class CooldownNode : public BTNode {
public:
    CooldownNode(BTNodeRef c, float s) : m_Child(std::move(c)), m_Cooldown(s) {}
    BTStatus Tick(float dt) override {
        m_Timer -= dt;
        if (m_Timer > 0.0f) return BTStatus::Failure;
        BTStatus s = m_Child->Tick(dt);
        if (s == BTStatus::Success) m_Timer = m_Cooldown;
        return s;
    }
private:
    BTNodeRef m_Child;
    float m_Cooldown, m_Timer = 0.0f;
};
} // namespace

namespace BT {
BTNodeRef Sequence(std::vector<BTNodeRef> c) { return MakeRef<SequenceNode>(std::move(c)); }
BTNodeRef Selector(std::vector<BTNodeRef> c) { return MakeRef<SelectorNode>(std::move(c)); }
BTNodeRef Parallel(std::vector<BTNodeRef> c) { return MakeRef<ParallelNode>(std::move(c)); }
BTNodeRef Action(std::function<BTStatus(float)> f) { return MakeRef<ActionNode>(std::move(f)); }
BTNodeRef Condition(std::function<bool()> f) { return MakeRef<ConditionNode>(std::move(f)); }
BTNodeRef Inverter(BTNodeRef c) { return MakeRef<InverterNode>(std::move(c)); }
BTNodeRef Repeat(BTNodeRef c, int n) { return MakeRef<RepeatNode>(std::move(c), n); }
BTNodeRef Wait(float s) { return MakeRef<WaitNode>(s); }
BTNodeRef Cooldown(BTNodeRef c, float s) { return MakeRef<CooldownNode>(std::move(c), s); }
}

} // namespace nd
