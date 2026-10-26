#include "nd/net/Network.h"
#include "nd/net/Socket.h"
#include "nd/scene/Entity.h"
#include "nd/scene/ScriptableEntity.h"
#include "nd/core/Log.h"
#include <chrono>
#include <map>

namespace nd {

std::string Network::playerPrefab = "Player";
float Network::sendRate = 20.0f;

namespace {
// Packet = 1 type byte + JSON payload. Text payloads keep the protocol easy to inspect;
// switch to a binary format once bandwidth matters.
enum PacketType : uint8_t { Hello = 1, Welcome, PeerJoined, PeerLeft, Snapshot, Message, Ping, Pong, Bye };

struct Peer {
    uint32_t id = 0;
    NetAddress addr;
    double lastHeard = 0.0;
};

enum class Mode { Off, Server, Client };

struct NetState {
    Mode mode = Mode::Off;
    uint32_t localId = 0;
    bool connected = false;
    NetAddress server;
    std::map<uint32_t, Peer> peers;              // server: connected clients
    std::vector<uint32_t> knownPeers;            // everybody (incl. local)
    std::map<uint32_t, EntityId> proxies;        // peer id -> proxy entity
    std::vector<uint32_t> pendingSpawns, pendingDespawns;
    std::vector<std::pair<std::string, std::pair<json, uint32_t>>> inbox;
    uint32_t nextClientId = 1;
    double sendTimer = 0.0, pingTimer = 0.0, lastServerHeard = 0.0, helloTimer = 0.0;
    double pingSent = 0.0;
    float ping = 0.0f;
    Scene* boundScene = nullptr;
};
NetState s;
UdpSocket g_Socket;   // kept outside NetState so the state can be reset by assignment

double Now() {
    static auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void SendPacket(const NetAddress& to, PacketType type, const json& payload) {
    std::string body = payload.dump();
    std::string packet(1, (char)type);
    packet += body;
    if (packet.size() > 60000) { ND_WARN("Network packet too large ({} bytes)", packet.size()); return; }
    g_Socket.Send(to, packet.data(), (int)packet.size());
}

void BroadcastToClients(PacketType type, const json& payload, uint32_t except = 0xFFFFFFFF) {
    for (auto& [id, p] : s.peers)
        if (id != except) SendPacket(p.addr, type, payload);
}

void AddKnown(uint32_t id) {
    if (std::find(s.knownPeers.begin(), s.knownPeers.end(), id) == s.knownPeers.end()) {
        s.knownPeers.push_back(id);
        if (id != s.localId) s.pendingSpawns.push_back(id);
    }
}
void RemoveKnown(uint32_t id) {
    s.knownPeers.erase(std::remove(s.knownPeers.begin(), s.knownPeers.end(), id), s.knownPeers.end());
    s.pendingDespawns.push_back(id);
}

std::string NetKey(Entity e) {
    auto& ni = e.Get<NetworkIdentityComponent>();
    if (e.Name().rfind("Player_", 0) == 0 || e.Name() == Network::playerPrefab) return "player:" + std::to_string(ni.owner);
    return std::to_string((uint64_t)e.GetUUID());
}

void ApplySnapshot(Scene& scene, const json& j) {
    for (auto& je : j.value("entities", json::array())) {
        std::string key = JGet<std::string>(je, "k", "");
        Entity e;
        if (key.rfind("player:", 0) == 0) {
            uint32_t owner = (uint32_t)std::stoul(key.substr(7));
            if (owner == s.localId) continue;
            auto it = s.proxies.find(owner);
            if (it != s.proxies.end()) e = Entity(it->second, &scene);
        } else {
            e = scene.FindByUUID(std::stoull(key));
        }
        if (!e || !e.Has<NetworkIdentityComponent>()) continue;
        auto& ni = e.Get<NetworkIdentityComponent>();
        if (ni.owner == s.localId) continue;
        ni.targetPosition = JGet(je, "p", e.Transform().position);
        ni.targetRotation = JGet(je, "r", e.Transform().rotation);
        if (!ni.hasTarget || !ni.interpolate) {
            e.Transform().position = ni.targetPosition;
            e.Transform().rotation = ni.targetRotation;
        }
        ni.hasTarget = true;
    }
}

void HandlePacket(const NetAddress& from, const uint8_t* data, int size, Scene& scene) {
    if (size < 1) return;
    PacketType type = (PacketType)data[0];
    json j;
    try { j = size > 1 ? json::parse(std::string((const char*)data + 1, size - 1)) : json::object(); } catch (...) { return; }

    if (s.mode == Mode::Server) {
        uint32_t sender = 0xFFFFFFFF;
        for (auto& [id, p] : s.peers) if (p.addr == from) { sender = id; p.lastHeard = Now(); }
        switch (type) {
        case Hello: {
            if (sender == 0xFFFFFFFF) {
                sender = s.nextClientId++;
                s.peers[sender] = { sender, from, Now() };
                ND_INFO("Client {} connected from {}", sender, from.ToString());
                BroadcastToClients(PeerJoined, { { "id", sender } }, sender);
                AddKnown(sender);
            }
            SendPacket(from, Welcome, { { "id", sender }, { "peers", s.knownPeers } });
            break;
        }
        case Snapshot:
            if (sender == 0xFFFFFFFF) return;
            ApplySnapshot(scene, j);
            BroadcastToClients(Snapshot, j, sender);
            break;
        case Message:
            if (sender == 0xFFFFFFFF) return;
            j["sender"] = sender;
            s.inbox.push_back({ JGet<std::string>(j, "channel", ""), { j.value("data", json()), sender } });
            BroadcastToClients(Message, j, sender);
            break;
        case Ping: SendPacket(from, Pong, j); break;
        case Bye:
            if (sender != 0xFFFFFFFF) {
                ND_INFO("Client {} left", sender);
                s.peers.erase(sender);
                RemoveKnown(sender);
                BroadcastToClients(PeerLeft, { { "id", sender } });
            }
            break;
        default: break;
        }
    } else if (s.mode == Mode::Client) {
        if (from != s.server) return;
        s.lastServerHeard = Now();
        switch (type) {
        case Welcome:
            if (!s.connected) {
                s.localId = JGet<uint32_t>(j, "id", 0);
                s.connected = true;
                s.knownPeers.clear();
                s.knownPeers.push_back(s.localId);
                for (auto& id : j.value("peers", json::array())) AddKnown(id.get<uint32_t>());
                ND_INFO("Connected to server as peer {}", s.localId);
            }
            break;
        case PeerJoined: AddKnown(JGet<uint32_t>(j, "id", 0)); break;
        case PeerLeft: RemoveKnown(JGet<uint32_t>(j, "id", 0)); break;
        case Snapshot: ApplySnapshot(scene, j); break;
        case Message: s.inbox.push_back({ JGet<std::string>(j, "channel", ""), { j.value("data", json()), JGet<uint32_t>(j, "sender", 0) } }); break;
        case Pong: s.ping = (float)((Now() - s.pingSent) * 1000.0); break;
        case Bye: ND_WARN("Server closed the connection"); Network::Disconnect(); break;
        default: break;
        }
    }
}

void SpawnProxies(Scene& scene) {
    Entity prefab = scene.FindByName(Network::playerPrefab);
    // Our own player
    if (prefab) {
        auto& ni = prefab.GetOrAdd<NetworkIdentityComponent>();
        ni.owner = s.localId;
    }
    for (uint32_t id : s.pendingSpawns) {
        if (s.proxies.count(id) || !prefab) continue;
        Entity e = scene.DuplicateEntity(prefab);
        if (!e) continue;
        e.Get<TagComponent>().name = "Player_" + std::to_string(id);
        auto& ni = e.GetOrAdd<NetworkIdentityComponent>();
        ni.owner = id;
        // Remote players are puppets: no camera/listener, kinematic physics.
        if (e.Has<CameraComponent>()) e.Remove<CameraComponent>();
        if (e.Has<AudioListenerComponent>()) e.Remove<AudioListenerComponent>();
        if (auto* rb = e.TryGet<RigidBodyComponent>()) rb->type = BodyType::Kinematic;
        for (auto& child : scene.GetChildren(e)) {
            if (child.Has<CameraComponent>()) child.Remove<CameraComponent>();
            if (child.Has<AudioListenerComponent>()) child.Remove<AudioListenerComponent>();
        }
        s.proxies[id] = e.Id();
        ND_INFO("Spawned remote player {}", id);
    }
    s.pendingSpawns.clear();
    for (uint32_t id : s.pendingDespawns) {
        auto it = s.proxies.find(id);
        if (it != s.proxies.end()) {
            scene.DestroyEntity(Entity(it->second, &scene));
            s.proxies.erase(it);
        }
    }
    s.pendingDespawns.clear();
}

json BuildSnapshot(Scene& scene) {
    json snap;
    snap["owner"] = s.localId;
    snap["entities"] = json::array();
    scene.Reg().View<TransformComponent, NetworkIdentityComponent>([&](EntityId id, TransformComponent& t, NetworkIdentityComponent& ni) {
        if (ni.owner != s.localId || !ni.syncTransform) return;
        snap["entities"].push_back({ { "k", NetKey(Entity(id, &scene)) }, { "p", t.position }, { "r", t.rotation } });
    });
    return snap;
}
} // namespace

bool Network::Host(uint16_t port) {
    Disconnect();
    if (!g_Socket.Open(port)) return false;
    s.mode = Mode::Server;
    s.localId = 0;
    s.connected = true;
    s.knownPeers = { 0 };
    s.nextClientId = 1;
    ND_INFO("Hosting on UDP port {}", port);
    return true;
}

bool Network::Connect(const std::string& address, uint16_t port) {
    Disconnect();
    NetAddress addr;
    if (!NetAddress::Resolve(address, port, addr)) {
        ND_ERROR("Could not resolve {}", address);
        return false;
    }
    if (!g_Socket.Open(0)) return false;
    s.mode = Mode::Client;
    s.server = addr;
    s.connected = false;
    s.lastServerHeard = Now();
    s.helloTimer = 0.0;
    ND_INFO("Connecting to {}...", addr.ToString());
    return true;
}

void Network::Disconnect() {
    if (s.mode == Mode::Server) BroadcastToClients(Bye, json::object());
    else if (s.mode == Mode::Client && s.connected) SendPacket(s.server, Bye, json::object());
    if (s.boundScene) {
        for (auto& [id, e] : s.proxies) s.boundScene->DestroyEntity(Entity(e, s.boundScene));
    }
    g_Socket.Close();
    Mode was = s.mode;
    s = NetState();
    if (was != Mode::Off) ND_INFO("Network session closed");
}

bool Network::IsActive() { return s.mode != Mode::Off; }
bool Network::IsServer() { return s.mode == Mode::Server; }
bool Network::IsConnected() { return s.mode != Mode::Off && s.connected; }
uint32_t Network::LocalId() { return s.localId; }
std::vector<uint32_t> Network::Peers() { return s.knownPeers; }
float Network::PingMs() { return s.ping; }

std::string Network::Status() {
    if (s.mode == Mode::Off) return "Offline";
    if (s.mode == Mode::Server) return "Hosting (" + std::to_string(s.peers.size()) + " client(s))";
    return s.connected ? "Connected as peer " + std::to_string(s.localId) : "Connecting...";
}

void Network::Send(const std::string& channel, const json& data) {
    json msg = { { "channel", channel }, { "data", data }, { "sender", s.localId } };
    if (s.mode == Mode::Server) BroadcastToClients(Message, msg);
    else if (s.mode == Mode::Client && s.connected) SendPacket(s.server, Message, msg);
}

bool Network::IsLocal(Entity e) {
    if (s.mode == Mode::Off || !e) return true;
    auto* ni = e.TryGet<NetworkIdentityComponent>();
    return !ni || ni->owner == s.localId;
}

void Network::Update(Scene& scene, float dt) {
    if (s.mode == Mode::Off) return;
    if (s.boundScene != &scene) {
        // New scene (scene switch): proxies must be re-created.
        s.proxies.clear();
        for (uint32_t id : s.knownPeers) if (id != s.localId) s.pendingSpawns.push_back(id);
        s.boundScene = &scene;
    }
    uint8_t buffer[65536];
    NetAddress from;
    int n;
    while ((n = g_Socket.Receive(from, buffer, sizeof(buffer))) > 0) HandlePacket(from, buffer, n, scene);

    double now = Now();
    if (s.mode == Mode::Client) {
        if (!s.connected) {
            s.helloTimer -= dt;
            if (s.helloTimer <= 0.0) { SendPacket(s.server, Hello, json::object()); s.helloTimer = 0.5; }
        }
        if (now - s.lastServerHeard > 6.0) {
            ND_WARN("Lost connection to server");
            Disconnect();
            return;
        }
    } else {
        for (auto it = s.peers.begin(); it != s.peers.end();) {
            if (now - it->second.lastHeard > 6.0) {
                ND_WARN("Client {} timed out", it->first);
                uint32_t id = it->first;
                it = s.peers.erase(it);
                RemoveKnown(id);
                BroadcastToClients(PeerLeft, { { "id", id } });
            } else ++it;
        }
    }
    if (!s.connected) return;

    SpawnProxies(scene);

    // Deliver messages to scripts
    auto inbox = std::move(s.inbox);
    s.inbox.clear();
    for (auto& msg : inbox) {
        const std::string& channel = msg.first;
        const json& data = msg.second.first;
        uint32_t sender = msg.second.second;
        scene.Reg().View<ScriptComponent>([&](EntityId, ScriptComponent& sc) {
            for (auto& si : sc.scripts) if (si.instance) si.instance->OnNetworkMessage(channel, data, sender);
        });
    }

    // Interpolate remote entities
    float k = 1.0f - std::exp(-15.0f * dt);
    scene.Reg().View<TransformComponent, NetworkIdentityComponent>([&](EntityId, TransformComponent& t, NetworkIdentityComponent& ni) {
        if (ni.owner == s.localId || !ni.hasTarget || !ni.interpolate) return;
        t.position = glm::mix(t.position, ni.targetPosition, k);
        t.rotation = glm::normalize(glm::slerp(t.rotation, ni.targetRotation, k));
    });

    // Send our state
    s.sendTimer -= dt;
    if (s.sendTimer <= 0.0) {
        s.sendTimer = 1.0 / std::max(1.0f, sendRate);
        json snap = BuildSnapshot(scene);
        if (!snap["entities"].empty()) {
            if (s.mode == Mode::Server) BroadcastToClients(Snapshot, snap);
            else SendPacket(s.server, Snapshot, snap);
        }
    }
    s.pingTimer -= dt;
    if (s.pingTimer <= 0.0) {
        s.pingTimer = 1.0;
        if (s.mode == Mode::Client) { s.pingSent = now; SendPacket(s.server, Ping, json::object()); }
        else BroadcastToClients(Ping, json::object());   // keeps clients' timeout alive
    }
}

} // namespace nd
