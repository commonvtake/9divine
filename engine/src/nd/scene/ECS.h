#pragma once
// A compact sparse-set Entity Component System.
//
//  * Entities are 32-bit handles: 20-bit index + 12-bit version (stale handles are detected).
//  * Each component type lives in its own pool; components are stored in a std::deque,
//    so references stay valid when other components of the same type are added.
//  * View<A, B>(fn) iterates the smallest pool and checks the others.
#include <cstdint>
#include <vector>
#include <deque>
#include <memory>
#include <cassert>
#include <utility>
#include <type_traits>

namespace nd {

using EntityId = uint32_t;
constexpr EntityId NullEntity = 0xFFFFFFFFu;

namespace ecs {
constexpr uint32_t kIndexBits = 20;
constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;
inline uint32_t Index(EntityId e) { return e & kIndexMask; }
inline uint32_t Version(EntityId e) { return e >> kIndexBits; }
inline EntityId Make(uint32_t index, uint32_t version) { return (version << kIndexBits) | index; }

inline uint32_t NextTypeId() { static uint32_t counter = 0; return counter++; }
template <typename T> uint32_t TypeId() { static const uint32_t id = NextTypeId(); return id; }
} // namespace ecs

class IPool {
public:
    virtual ~IPool() = default;
    virtual bool Has(EntityId e) const = 0;
    virtual void Remove(EntityId e) = 0;
    virtual size_t Size() const = 0;
    virtual const std::vector<EntityId>& Entities() const = 0;
    virtual void Clear() = 0;
};

template <typename T>
class Pool final : public IPool {
public:
    bool Has(EntityId e) const override {
        uint32_t i = ecs::Index(e);
        return i < m_Sparse.size() && m_Sparse[i] != kNone && m_Dense[m_Sparse[i]] == e;
    }

    template <typename... Args>
    T& Emplace(EntityId e, Args&&... args) {
        uint32_t i = ecs::Index(e);
        if (i >= m_Sparse.size()) m_Sparse.resize(i + 1, kNone);
        if (m_Sparse[i] != kNone && m_Dense[m_Sparse[i]] == e) {
            m_Data[m_Sparse[i]] = T{ std::forward<Args>(args)... };
            return m_Data[m_Sparse[i]];
        }
        m_Sparse[i] = (uint32_t)m_Dense.size();
        m_Dense.push_back(e);
        if constexpr (std::is_aggregate_v<T>) m_Data.push_back(T{ std::forward<Args>(args)... });
        else m_Data.emplace_back(std::forward<Args>(args)...);
        return m_Data.back();
    }

    T& Get(EntityId e) {
        assert(Has(e) && "Entity does not have this component");
        return m_Data[m_Sparse[ecs::Index(e)]];
    }
    T* TryGet(EntityId e) { return Has(e) ? &m_Data[m_Sparse[ecs::Index(e)]] : nullptr; }

    void Remove(EntityId e) override {
        if (!Has(e)) return;
        uint32_t i = ecs::Index(e);
        uint32_t slot = m_Sparse[i];
        uint32_t last = (uint32_t)m_Dense.size() - 1;
        if (slot != last) {
            m_Dense[slot] = m_Dense[last];
            m_Data[slot] = std::move(m_Data[last]);
            m_Sparse[ecs::Index(m_Dense[slot])] = slot;
        }
        m_Dense.pop_back();
        m_Data.pop_back();
        m_Sparse[i] = kNone;
    }

    size_t Size() const override { return m_Dense.size(); }
    const std::vector<EntityId>& Entities() const override { return m_Dense; }
    void Clear() override { m_Dense.clear(); m_Data.clear(); m_Sparse.clear(); }

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    std::vector<uint32_t> m_Sparse;
    std::vector<EntityId> m_Dense;
    std::deque<T> m_Data;
};

class Registry {
public:
    EntityId Create() {
        if (!m_Free.empty()) {
            uint32_t index = m_Free.back();
            m_Free.pop_back();
            m_Alive[index] = true;
            return ecs::Make(index, m_Versions[index]);
        }
        uint32_t index = (uint32_t)m_Versions.size();
        m_Versions.push_back(0);
        m_Alive.push_back(true);
        return ecs::Make(index, 0);
    }

    void Destroy(EntityId e) {
        if (!Valid(e)) return;
        for (auto& p : m_Pools) if (p) p->Remove(e);
        uint32_t i = ecs::Index(e);
        m_Versions[i] = (m_Versions[i] + 1) & 0xFFFu;
        m_Alive[i] = false;
        m_Free.push_back(i);
    }

    bool Valid(EntityId e) const {
        if (e == NullEntity) return false;
        uint32_t i = ecs::Index(e);
        return i < m_Versions.size() && m_Alive[i] && m_Versions[i] == ecs::Version(e);
    }

    void Clear() {
        for (auto& p : m_Pools) if (p) p->Clear();
        m_Versions.clear();
        m_Alive.clear();
        m_Free.clear();
    }

    template <typename T, typename... Args> T& Add(EntityId e, Args&&... args) { return GetPool<T>().Emplace(e, std::forward<Args>(args)...); }
    template <typename T> T& Get(EntityId e) { return GetPool<T>().Get(e); }
    template <typename T> T* TryGet(EntityId e) { auto* p = FindPool<T>(); return p ? p->TryGet(e) : nullptr; }
    template <typename T> bool Has(EntityId e) const { auto* p = FindPool<T>(); return p && p->Has(e); }
    template <typename T> void Remove(EntityId e) { if (auto* p = FindPool<T>()) p->Remove(e); }
    template <typename... T> bool HasAll(EntityId e) const { return (Has<T>(e) && ...); }

    // Calls fn(EntityId, A&, B&...) for every entity that has all listed components.
    // Iterates over a snapshot of entity ids, so destroying entities inside is safe.
    template <typename First, typename... Rest, typename Fn>
    void View(Fn&& fn) {
        const IPool* pools[] = { FindPool<First>(), FindPool<Rest>()... };
        const IPool* smallest = nullptr;
        for (const IPool* p : pools) {
            if (!p) return;
            if (!smallest || p->Size() < smallest->Size()) smallest = p;
        }
        std::vector<EntityId> ids = smallest->Entities();
        for (EntityId e : ids) {
            if (!Valid(e) || !HasAll<First, Rest...>(e)) continue;
            fn(e, Get<First>(e), Get<Rest>(e)...);
        }
    }

    template <typename T> size_t Count() const { auto* p = FindPool<T>(); return p ? p->Size() : 0; }

    // Every live entity (in creation-slot order).
    std::vector<EntityId> AllEntities() const {
        std::vector<EntityId> out;
        for (uint32_t i = 0; i < m_Versions.size(); i++)
            if (m_Alive[i]) out.push_back(ecs::Make(i, m_Versions[i]));
        return out;
    }
    size_t AliveCount() const { return m_Versions.size() - m_Free.size(); }

private:
    template <typename T> Pool<T>& GetPool() {
        uint32_t id = ecs::TypeId<T>();
        if (id >= m_Pools.size()) m_Pools.resize(id + 1);
        if (!m_Pools[id]) m_Pools[id] = std::make_unique<Pool<T>>();
        return *static_cast<Pool<T>*>(m_Pools[id].get());
    }
    template <typename T> Pool<T>* FindPool() const {
        uint32_t id = ecs::TypeId<T>();
        return id < m_Pools.size() ? static_cast<Pool<T>*>(m_Pools[id].get()) : nullptr;
    }

    std::vector<std::unique_ptr<IPool>> m_Pools;
    std::vector<uint32_t> m_Versions;
    std::vector<bool> m_Alive;
    std::vector<uint32_t> m_Free;
};

} // namespace nd
