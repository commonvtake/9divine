#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/core/UUID.h"
#include "euphoria/core/Json.h"
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace eu {

// A named game event with an optional JSON payload.
struct Event {
    std::string name;
    json data;
    UUID sender = 0;            // entity that emitted it (0 = none)
    double time = 0.0;          // scene time when emitted
};

// Per-scene event bus and timer service.
//
//   events.On("DoorOpened", [](const Event& e) { ... });     // subscribe ("*" = every event)
//   events.Emit("DoorOpened", { { "door", 3 } });             // queue; delivered this frame
//   events.Invoke(2.0f, [] { ... });                          // run once in 2 s
//   events.InvokeRepeating(0.0f, 1.0f, [] { ... });           // every second
//
// Emitted events are queued and delivered in Update(), after scripts and physics have run,
// so listeners never run in the middle of another system. Events emitted by listeners are
// delivered in the same Update (up to 8 rounds, so two listeners can't ping-pong forever).
// Timers use scaled game time and stop while the scene is paused.
class EventSystem {
public:
    using Id = uint64_t;
    using Handler = std::function<void(const Event&)>;

    Id On(const std::string& name, Handler handler, UUID owner = 0);
    void Off(Id id);
    void OffAll(UUID owner);                       // everything an entity subscribed/scheduled

    void Emit(const std::string& name, const json& data = json::object(), UUID sender = 0);

    Id Invoke(float delay, std::function<void()> fn, UUID owner = 0);
    Id InvokeRepeating(float delay, float interval, std::function<void()> fn, UUID owner = 0);
    void Cancel(Id id);

    void Update(float dt);                         // advance timers, deliver queued events
    void Clear();                                  // drop listeners, timers and the queue

    double Time() const { return m_Time; }
    size_t ListenerCount(const std::string& name) const;
    size_t PendingTimers() const { return m_Timers.size(); }

    // Recent events, newest last, for the editor's Event Log window.
    struct LogEntry {
        double time;
        std::string name, data;
        UUID sender;
        int listeners;
    };
    const std::deque<LogEntry>& Log() const { return m_Log; }
    void ClearLog() { m_Log.clear(); }
    static constexpr size_t kLogSize = 300;

private:
    struct Listener { Id id; std::string name; Handler fn; UUID owner; bool removed = false; };
    struct Timer { Id id; double due; float interval; std::function<void()> fn; UUID owner; bool removed = false; };
    std::vector<Listener> m_Listeners;
    std::vector<Timer> m_Timers;
    std::vector<Event> m_Queue;
    std::deque<LogEntry> m_Log;
    double m_Time = 0.0;
    Id m_NextId = 1;
    int m_Dispatching = 0;
};

} // namespace eu
