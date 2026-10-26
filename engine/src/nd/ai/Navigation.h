#pragma once
#include "nd/core/Base.h"

namespace nd {

class Scene;

// Grid-based navigation mesh. Bake() samples the terrain and static colliders into
// walkable cells; FindPath() runs A* (8-way) and smooths the result with line-of-sight checks.
class NavGrid {
public:
    struct Settings {
        float cellSize = 0.5f;
        float agentRadius = 0.4f;
        float agentHeight = 1.8f;
        float maxSlope = 40.0f;          // degrees
        float maxStep = 0.45f;           // height difference between neighbouring cells
        float minWalkableHeight = -1e9f; // e.g. water level
        vec3 boundsMin{ -60.0f, -10.0f, -60.0f };
        vec3 boundsMax{ 60.0f, 50.0f, 60.0f };
        bool autoBounds = true;          // fit to terrain/colliders
    };

    explicit NavGrid(Scene* scene) : m_Scene(scene) {}

    void Bake();
    bool IsBaked() const { return !m_Cells.empty(); }
    void Clear() { m_Cells.clear(); }
    bool FindPath(const vec3& start, const vec3& end, std::vector<vec3>& outPath);
    bool IsWalkable(const vec3& p) const;
    bool SampleHeight(const vec3& p, float& h) const;
    vec3 ClosestWalkable(const vec3& p) const;
    vec3 RandomPoint(uint32_t& seed) const;
    void DebugDraw() const;

    Settings settings;
    int Width() const { return m_W; }
    int Depth() const { return m_D; }

private:
    struct Cell { bool walkable = false; float height = 0.0f; };
    bool ToCell(const vec3& p, int& x, int& z) const;
    vec3 CellCenter(int x, int z) const;
    bool LineOfSight(int x0, int z0, int x1, int z1) const;
    const Cell* At(int x, int z) const { return (x < 0 || z < 0 || x >= m_W || z >= m_D) ? nullptr : &m_Cells[z * m_W + x]; }

    Scene* m_Scene;
    std::vector<Cell> m_Cells;
    int m_W = 0, m_D = 0;
    vec3 m_Origin{ 0.0f };
};

// ---------------------------------------------------------------------------
// Finite state machine for AI. States are lambdas; owner-agnostic.
//   fsm.Add("Patrol", { onEnter, onUpdate, onExit });  fsm.Change("Patrol");
class StateMachine {
public:
    struct State {
        std::function<void()> enter;
        std::function<void(float)> update;
        std::function<void()> exit;
    };
    void Add(const std::string& name, State s) { m_States[name] = std::move(s); }
    void Change(const std::string& name) {
        if (name == m_Current && m_Started) return;
        if (m_Started) { auto it = m_States.find(m_Current); if (it != m_States.end() && it->second.exit) it->second.exit(); }
        m_Current = name;
        m_Started = true;
        m_TimeInState = 0.0f;
        auto it = m_States.find(name);
        if (it != m_States.end() && it->second.enter) it->second.enter();
    }
    void Update(float dt) {
        m_TimeInState += dt;
        auto it = m_States.find(m_Current);
        if (it != m_States.end() && it->second.update) it->second.update(dt);
    }
    const std::string& Current() const { return m_Current; }
    float TimeInState() const { return m_TimeInState; }
private:
    std::unordered_map<std::string, State> m_States;
    std::string m_Current;
    bool m_Started = false;
    float m_TimeInState = 0.0f;
};

// ---------------------------------------------------------------------------
// Behaviour trees. Build with the helpers in BT:: and tick every frame.
//   auto tree = BT::Selector({ BT::Sequence({ BT::Condition(seesPlayer), BT::Action(chase) }),
//                              BT::Action(wander) });
//   tree->Tick(dt);
enum class BTStatus { Success, Failure, Running };

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus Tick(float dt) = 0;
    virtual void Reset() {}
};
using BTNodeRef = Ref<BTNode>;

namespace BT {
BTNodeRef Sequence(std::vector<BTNodeRef> children);     // runs children in order until one fails
BTNodeRef Selector(std::vector<BTNodeRef> children);     // first child that doesn't fail
BTNodeRef Parallel(std::vector<BTNodeRef> children);     // ticks all; succeeds when all succeed
BTNodeRef Action(std::function<BTStatus(float)> fn);
BTNodeRef Condition(std::function<bool()> fn);
BTNodeRef Inverter(BTNodeRef child);
BTNodeRef Repeat(BTNodeRef child, int times = -1);       // -1 = forever
BTNodeRef Wait(float seconds);
BTNodeRef Cooldown(BTNodeRef child, float seconds);
}

} // namespace nd
