#pragma once
#include "nd/scene/Scene.h"

namespace nd {

// Lightweight handle (scene pointer + id). Cheap to copy; check with IsValid() / operator bool.
class Entity {
public:
    Entity() = default;
    Entity(EntityId id, Scene* scene) : m_Id(id), m_Scene(scene) {}

    template <typename T, typename... Args>
    T& Add(Args&&... args) { return m_Scene->m_Registry.Add<T>(m_Id, std::forward<Args>(args)...); }
    template <typename T>
    T& GetOrAdd() { auto* c = m_Scene->m_Registry.TryGet<T>(m_Id); return c ? *c : Add<T>(); }
    template <typename T> T& Get() { return m_Scene->m_Registry.Get<T>(m_Id); }
    template <typename T> T* TryGet() { return m_Scene ? m_Scene->m_Registry.TryGet<T>(m_Id) : nullptr; }
    template <typename T> bool Has() const { return m_Scene && m_Scene->m_Registry.Has<T>(m_Id); }
    template <typename T> void Remove() { m_Scene->m_Registry.Remove<T>(m_Id); }

    bool IsValid() const { return m_Scene && m_Scene->m_Registry.Valid(m_Id); }
    explicit operator bool() const { return IsValid(); }
    bool operator==(const Entity& o) const { return m_Id == o.m_Id && m_Scene == o.m_Scene; }
    bool operator!=(const Entity& o) const { return !(*this == o); }

    EntityId Id() const { return m_Id; }
    Scene* GetScene() const { return m_Scene; }
    UUID GetUUID() { return Get<IDComponent>().id; }
    const std::string& Name() { return Get<TagComponent>().name; }
    TransformComponent& Transform() { return Get<TransformComponent>(); }
    bool IsActive() { return m_Scene->IsActiveInHierarchy(*this); }
    void SetActive(bool a) { Get<TagComponent>().active = a; }

private:
    EntityId m_Id = NullEntity;
    Scene* m_Scene = nullptr;
};

} // namespace nd
