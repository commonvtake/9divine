#pragma once
#include "nd/core/Base.h"
#include "nd/core/Json.h"
#include "nd/scene/ECS.h"

namespace nd {

class Scene;
class Entity;

// Client/server multiplayer over UDP.
//  * Host() starts a server (the host also plays, as peer 0). Connect() joins one.
//  * Every peer gets a copy of the scene's "player prefab" entity; the local copy is driven
//    by input, remote copies are interpolated from snapshots (20 Hz by default).
//  * Entities with a NetworkIdentity owned by this peer are replicated automatically.
//  * Send() broadcasts JSON messages to every peer (scripts get OnNetworkMessage).
class Network {
public:
    static bool Host(uint16_t port = 7777);
    static bool Connect(const std::string& address, uint16_t port = 7777);
    static void Disconnect();

    static bool IsActive();
    static bool IsServer();
    static bool IsConnected();               // client: handshake finished
    static uint32_t LocalId();               // host = 0, clients = 1..N
    static std::vector<uint32_t> Peers();    // every peer including us
    static std::string Status();
    static float PingMs();

    static void Send(const std::string& channel, const json& data);

    // True if the entity is controlled by this machine (always true when offline).
    static bool IsLocal(Entity e);

    static void Update(Scene& scene, float dt);   // called by Scene each frame

    static std::string playerPrefab;          // name of the entity cloned for each peer
    static float sendRate;                    // snapshots per second
};

} // namespace nd
