#include "euphoria/scene/Events.h"
#include "euphoria/core/Log.h"
#include <algorithm>

namespace eu {

EventSystem::Id EventSystem::On(const std::string& name, Handler handler, UUID owner) {
    Id id = m_NextId++;
    m_Listeners.push_back({ id, name, std::move(handler), owner });
    return id;
}

void EventSystem::Off(Id id) {
    for (auto& l : m_Listeners) if (l.id == id) l.removed = true;
    for (auto& t : m_Timers) if (t.id == id) t.removed = true;
}

void EventSystem::OffAll(UUID owner) {
    if (!owner) return;
    for (auto& l : m_Listeners) if (l.owner == owner) l.removed = true;
    for (auto& t : m_Timers) if (t.owner == owner) t.removed = true;
}

void EventSystem::Emit(const std::string& name, const json& data, UUID sender) {
    m_Queue.push_back({ name, data, sender, m_Time });
}

EventSystem::Id EventSystem::Invoke(float delay, std::function<void()> fn, UUID owner) {
    Id id = m_NextId++;
    m_Timers.push_back({ id, m_Time + std::max(0.0f, delay), 0.0f, std::move(fn), owner });
    return id;
}

EventSystem::Id EventSystem::InvokeRepeating(float delay, float interval, std::function<void()> fn, UUID owner) {
    Id id = m_NextId++;
    m_Timers.push_back({ id, m_Time + std::max(0.0f, delay), std::max(interval, 0.001f), std::move(fn), owner });
    return id;
}

void EventSystem::Cancel(Id id) { Off(id); }

size_t EventSystem::ListenerCount(const std::string& name) const {
    size_t n = 0;
    for (auto& l : m_Listeners) if (!l.removed && (l.name == name || l.name == "*")) n++;
    return n;
}

void EventSystem::Update(float dt) {
    m_Time += dt;

    // Timers (callbacks may schedule more timers: iterate by index over a growing vector)
    for (size_t i = 0; i < m_Timers.size(); i++) {
        if (m_Timers[i].removed || m_Timers[i].due > m_Time) continue;
        auto fn = m_Timers[i].fn;              // copy: the vector may grow inside the call
        if (m_Timers[i].interval > 0.0f) {
            // Catch up at most a few intervals after a hitch, then skip ahead
            int fired = 0;
            while (!m_Timers[i].removed && m_Timers[i].due <= m_Time && fired < 4) {
                m_Timers[i].due += m_Timers[i].interval;
                fired++;
                fn();
            }
            if (m_Timers[i].due <= m_Time) m_Timers[i].due = m_Time + m_Timers[i].interval;
        } else {
            m_Timers[i].removed = true;
            fn();
        }
    }
    m_Timers.erase(std::remove_if(m_Timers.begin(), m_Timers.end(), [](const Timer& t) { return t.removed; }), m_Timers.end());

    // Events
    for (int round = 0; round < 8 && !m_Queue.empty(); round++) {
        std::vector<Event> batch;
        batch.swap(m_Queue);
        m_Dispatching++;
        for (const Event& e : batch) {
            int delivered = 0;
            for (size_t i = 0; i < m_Listeners.size(); i++) {   // index: handlers may subscribe
                if (m_Listeners[i].removed) continue;
                if (m_Listeners[i].name != e.name && m_Listeners[i].name != "*") continue;
                auto fn = m_Listeners[i].fn;
                fn(e);
                delivered++;
            }
            m_Log.push_back({ e.time, e.name, e.data.is_null() || e.data.empty() ? std::string() : e.data.dump(), e.sender, delivered });
            while (m_Log.size() > kLogSize) m_Log.pop_front();
        }
        m_Dispatching--;
    }
    if (!m_Queue.empty()) EU_WARN("Events are re-emitting each other; {} left for next frame", m_Queue.size());
    m_Listeners.erase(std::remove_if(m_Listeners.begin(), m_Listeners.end(), [](const Listener& l) { return l.removed; }), m_Listeners.end());
}

void EventSystem::Clear() {
    m_Listeners.clear();
    m_Timers.clear();
    m_Queue.clear();
    m_Time = 0.0;
}

} // namespace eu
