#include "nd/scene/SceneSerializer.h"
#include "nd/scene/ScriptableEntity.h"
#include "nd/ai/Navigation.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"

namespace nd {

namespace {
template <typename E, size_t N>
std::string EnumStr(E v, const char* const (&names)[N]) { int i = (int)v; return i >= 0 && i < (int)N ? names[i] : names[0]; }
template <typename E, size_t N>
E EnumFrom(const json& j, const char* key, E fallback, const char* const (&names)[N]) {
    std::string s = JGet<std::string>(j, key, "");
    for (size_t i = 0; i < N; i++) if (s == names[i]) return (E)i;
    return fallback;
}
const char* kLightTypes[] = { "Directional", "Point", "Spot" };
const char* kBodyTypes[] = { "Static", "Dynamic", "Kinematic" };
const char* kParamTypes[] = { "Float", "Bool", "Trigger" };
const char* kCondOps[] = { "Greater", "Less", "IsTrue", "IsFalse", "Trigger" };
const char* kShapes[] = { "Point", "Sphere", "Box" };
const char* kUITypes[] = { "Panel", "Text", "Button", "ProgressBar", "Image" };
const char* kAnchors[] = { "TopLeft", "Top", "TopRight", "Left", "Center", "Right", "BottomLeft", "Bottom", "BottomRight" };
} // namespace

json SceneSerializer::SerializeEntity(Entity e) {
    json j;
    j["uuid"] = (uint64_t)e.GetUUID();
    auto& tag = e.Get<TagComponent>();
    j["name"] = tag.name;
    j["tag"] = tag.tag;
    j["active"] = tag.active;

    auto& t = e.Get<TransformComponent>();
    j["transform"] = { { "position", t.position }, { "rotation", t.rotation }, { "scale", t.scale }, { "parent", (uint64_t)t.parent } };

    if (auto* c = e.TryGet<MeshRendererComponent>())
        j["meshRenderer"] = { { "mesh", c->mesh }, { "useModelMaterials", c->useModelMaterials }, { "color", c->color }, { "texture", c->texture },
                              { "tiling", c->tiling }, { "specular", c->specular }, { "shininess", c->shininess }, { "emissive", c->emissive },
                              { "castShadows", c->castShadows }, { "visible", c->visible } };
    if (auto* c = e.TryGet<CameraComponent>())
        j["camera"] = { { "primary", c->primary }, { "orthographic", c->orthographic }, { "fov", c->fov }, { "orthoSize", c->orthoSize },
                        { "near", c->nearClip }, { "far", c->farClip } };
    if (auto* c = e.TryGet<LightComponent>())
        j["light"] = { { "type", EnumStr(c->type, kLightTypes) }, { "color", c->color }, { "intensity", c->intensity }, { "range", c->range },
                       { "spotAngle", c->spotAngle }, { "castShadows", c->castShadows } };
    if (auto* c = e.TryGet<RigidBodyComponent>())
        j["rigidBody"] = { { "type", EnumStr(c->type, kBodyTypes) }, { "mass", c->mass }, { "drag", c->drag }, { "gravityScale", c->gravityScale },
                           { "restitution", c->restitution }, { "friction", c->friction }, { "lockY", c->lockY } };
    if (auto* c = e.TryGet<BoxColliderComponent>())
        j["boxCollider"] = { { "size", c->size }, { "offset", c->offset }, { "isTrigger", c->isTrigger } };
    if (auto* c = e.TryGet<SphereColliderComponent>())
        j["sphereCollider"] = { { "radius", c->radius }, { "offset", c->offset }, { "isTrigger", c->isTrigger } };
    if (auto* c = e.TryGet<ScriptComponent>()) {
        json arr = json::array();
        for (auto& s : c->scripts) {
            json fields = s.instance ? SaveScriptFields(*s.instance) : s.fields;
            arr.push_back({ { "class", s.className }, { "fields", fields } });
        }
        j["scripts"] = arr;
    }
    if (auto* c = e.TryGet<AudioSourceComponent>())
        j["audioSource"] = { { "clip", c->clip }, { "volume", c->volume }, { "pitch", c->pitch }, { "loop", c->loop }, { "playOnStart", c->playOnStart },
                             { "spatial", c->spatial }, { "music", c->music }, { "minDistance", c->minDistance }, { "maxDistance", c->maxDistance } };
    if (auto* c = e.TryGet<AudioListenerComponent>()) j["audioListener"] = { { "active", c->active } };
    if (auto* c = e.TryGet<AnimatorComponent>()) {
        json a;
        a["defaultState"] = c->defaultState;
        a["speed"] = c->speed;
        a["rootMotion"] = c->rootMotion;
        a["params"] = json::array();
        for (auto& p : c->params) a["params"].push_back({ { "name", p.name }, { "type", EnumStr(p.type, kParamTypes) }, { "value", p.type == AnimParamType::Trigger ? 0.0f : p.value } });
        a["states"] = json::array();
        for (auto& s : c->states) {
            json js = { { "name", s.name }, { "clip", s.clip }, { "speed", s.speed }, { "loop", s.loop }, { "blendParam", s.blendParam }, { "blend", json::array() } };
            for (auto& b : s.blend) js["blend"].push_back({ { "clip", b.clip }, { "threshold", b.threshold } });
            a["states"].push_back(js);
        }
        a["transitions"] = json::array();
        for (auto& tr : c->transitions) {
            json jt = { { "from", tr.from }, { "to", tr.to }, { "duration", tr.duration }, { "exitTime", tr.exitTime }, { "conditions", json::array() } };
            for (auto& cd : tr.conditions) jt["conditions"].push_back({ { "param", cd.param }, { "op", EnumStr(cd.op, kCondOps) }, { "threshold", cd.threshold } });
            a["transitions"].push_back(jt);
        }
        j["animator"] = a;
    }
    if (auto* c = e.TryGet<TerrainComponent>()) {
        j["terrain"] = { { "resolution", c->resolution }, { "size", c->size }, { "heightScale", c->heightScale }, { "seed", c->seed },
                         { "noiseScale", c->noiseScale }, { "octaves", c->octaves }, { "sandColor", c->sandColor }, { "grassColor", c->grassColor },
                         { "rockColor", c->rockColor }, { "snowColor", c->snowColor }, { "sandHeight", c->sandHeight }, { "snowHeight", c->snowHeight },
                         { "grass", c->grass }, { "grassDensity", c->grassDensity }, { "grassMinHeight", c->grassMinHeight },
                         { "treeCount", c->treeCount }, { "treeModel", c->treeModel } };
        if (c->heights.size() == (size_t)c->resolution * c->resolution)
            j["terrain"]["heights"] = Base64Encode(c->heights.data(), c->heights.size() * sizeof(float));
    }
    if (auto* c = e.TryGet<WaterComponent>())
        j["water"] = { { "size", c->size }, { "shallowColor", c->shallowColor }, { "deepColor", c->deepColor }, { "waveHeight", c->waveHeight },
                       { "waveSpeed", c->waveSpeed }, { "waveScale", c->waveScale } };
    if (auto* c = e.TryGet<ParticleEmitterComponent>())
        j["particles"] = { { "playOnStart", c->playOnStart }, { "loop", c->loop }, { "rate", c->rate }, { "burst", c->burst }, { "maxParticles", c->maxParticles },
                           { "lifetime", c->lifetime }, { "lifetimeVariance", c->lifetimeVariance }, { "shape", EnumStr(c->shape, kShapes) },
                           { "shapeSize", c->shapeSize }, { "velocity", c->velocity }, { "spread", c->spread }, { "gravity", c->gravity },
                           { "drag", c->drag }, { "startColor", c->startColor }, { "endColor", c->endColor }, { "startSize", c->startSize },
                           { "endSize", c->endSize }, { "additive", c->additive }, { "worldSpace", c->worldSpace } };
    if (auto* c = e.TryGet<UIElementComponent>())
        j["ui"] = { { "type", EnumStr(c->type, kUITypes) }, { "anchor", EnumStr(c->anchor, kAnchors) }, { "position", c->position }, { "size", c->size },
                    { "color", c->color }, { "textColor", c->textColor }, { "text", c->text }, { "fontSize", c->fontSize }, { "value", c->value },
                    { "image", c->image }, { "visible", c->visible }, { "layer", c->layer } };
    if (auto* c = e.TryGet<NavAgentComponent>())
        j["navAgent"] = { { "speed", c->speed }, { "turnSpeed", c->turnSpeed }, { "stoppingDistance", c->stoppingDistance },
                          { "repathInterval", c->repathInterval }, { "snapToGround", c->snapToGround }, { "baseOffset", c->baseOffset } };
    if (auto* c = e.TryGet<NetworkIdentityComponent>())
        j["network"] = { { "owner", c->owner }, { "syncTransform", c->syncTransform }, { "interpolate", c->interpolate } };
    if (auto* c = e.TryGet<SaveableComponent>())
        j["saveable"] = { { "transform", c->saveTransform }, { "scripts", c->saveScripts }, { "active", c->saveActive } };
    return j;
}

Entity SceneSerializer::DeserializeEntity(Scene& scene, const json& j, std::unordered_map<uint64_t, uint64_t>* remap) {
    uint64_t id = JGet<uint64_t>(j, "uuid", 0);
    if (remap) {
        uint64_t fresh = UUID();
        (*remap)[id] = fresh;
        id = fresh;
    }
    Entity e = id ? scene.CreateEntityWithUUID(id, JGet<std::string>(j, "name", "Entity")) : scene.CreateEntity(JGet<std::string>(j, "name", "Entity"));
    auto& tag = e.Get<TagComponent>();
    tag.tag = JGet<std::string>(j, "tag", "Untagged");
    tag.active = JGet(j, "active", true);

    if (j.contains("transform")) {
        auto& jt = j["transform"];
        auto& t = e.Get<TransformComponent>();
        t.position = JGet(jt, "position", vec3(0.0f));
        t.rotation = JGet(jt, "rotation", quat(1, 0, 0, 0));
        t.scale = JGet(jt, "scale", vec3(1.0f));
        uint64_t parent = JGet<uint64_t>(jt, "parent", 0);
        if (remap && parent && remap->count(parent)) parent = (*remap)[parent];
        t.parent = parent;
    }
    if (j.contains("meshRenderer")) {
        auto& jc = j["meshRenderer"];
        auto& c = e.Add<MeshRendererComponent>();
        c.mesh = JGet<std::string>(jc, "mesh", c.mesh);
        c.useModelMaterials = JGet(jc, "useModelMaterials", c.useModelMaterials);
        c.color = JGet(jc, "color", c.color);
        c.texture = JGet<std::string>(jc, "texture", "");
        c.tiling = JGet(jc, "tiling", c.tiling);
        c.specular = JGet(jc, "specular", c.specular);
        c.shininess = JGet(jc, "shininess", c.shininess);
        c.emissive = JGet(jc, "emissive", c.emissive);
        c.castShadows = JGet(jc, "castShadows", c.castShadows);
        c.visible = JGet(jc, "visible", c.visible);
    }
    if (j.contains("camera")) {
        auto& jc = j["camera"];
        auto& c = e.Add<CameraComponent>();
        c.primary = JGet(jc, "primary", c.primary);
        c.orthographic = JGet(jc, "orthographic", c.orthographic);
        c.fov = JGet(jc, "fov", c.fov);
        c.orthoSize = JGet(jc, "orthoSize", c.orthoSize);
        c.nearClip = JGet(jc, "near", c.nearClip);
        c.farClip = JGet(jc, "far", c.farClip);
    }
    if (j.contains("light")) {
        auto& jc = j["light"];
        auto& c = e.Add<LightComponent>();
        c.type = EnumFrom(jc, "type", c.type, kLightTypes);
        c.color = JGet(jc, "color", c.color);
        c.intensity = JGet(jc, "intensity", c.intensity);
        c.range = JGet(jc, "range", c.range);
        c.spotAngle = JGet(jc, "spotAngle", c.spotAngle);
        c.castShadows = JGet(jc, "castShadows", c.castShadows);
    }
    if (j.contains("rigidBody")) {
        auto& jc = j["rigidBody"];
        auto& c = e.Add<RigidBodyComponent>();
        c.type = EnumFrom(jc, "type", c.type, kBodyTypes);
        c.mass = JGet(jc, "mass", c.mass);
        c.drag = JGet(jc, "drag", c.drag);
        c.gravityScale = JGet(jc, "gravityScale", c.gravityScale);
        c.restitution = JGet(jc, "restitution", c.restitution);
        c.friction = JGet(jc, "friction", c.friction);
        c.lockY = JGet(jc, "lockY", c.lockY);
    }
    if (j.contains("boxCollider")) {
        auto& jc = j["boxCollider"];
        auto& c = e.Add<BoxColliderComponent>();
        c.size = JGet(jc, "size", c.size);
        c.offset = JGet(jc, "offset", c.offset);
        c.isTrigger = JGet(jc, "isTrigger", c.isTrigger);
    }
    if (j.contains("sphereCollider")) {
        auto& jc = j["sphereCollider"];
        auto& c = e.Add<SphereColliderComponent>();
        c.radius = JGet(jc, "radius", c.radius);
        c.offset = JGet(jc, "offset", c.offset);
        c.isTrigger = JGet(jc, "isTrigger", c.isTrigger);
    }
    if (j.contains("scripts")) {
        auto& c = e.Add<ScriptComponent>();
        for (auto& js : j["scripts"]) {
            ScriptInstance s;
            s.className = JGet<std::string>(js, "class", "");
            s.fields = js.contains("fields") ? js["fields"] : json::object();
            if (remap) {
                // Entity references inside fields that point into the duplicated set get remapped.
                for (auto& [k, v] : s.fields.items())
                    if (v.is_number_unsigned() && remap->count(v.get<uint64_t>())) v = (*remap)[v.get<uint64_t>()];
            }
            c.scripts.push_back(std::move(s));
        }
    }
    if (j.contains("audioSource")) {
        auto& jc = j["audioSource"];
        auto& c = e.Add<AudioSourceComponent>();
        c.clip = JGet<std::string>(jc, "clip", "");
        c.volume = JGet(jc, "volume", c.volume);
        c.pitch = JGet(jc, "pitch", c.pitch);
        c.loop = JGet(jc, "loop", c.loop);
        c.playOnStart = JGet(jc, "playOnStart", c.playOnStart);
        c.spatial = JGet(jc, "spatial", c.spatial);
        c.music = JGet(jc, "music", c.music);
        c.minDistance = JGet(jc, "minDistance", c.minDistance);
        c.maxDistance = JGet(jc, "maxDistance", c.maxDistance);
    }
    if (j.contains("audioListener")) e.Add<AudioListenerComponent>().active = JGet(j["audioListener"], "active", true);
    if (j.contains("animator")) {
        auto& ja = j["animator"];
        auto& c = e.Add<AnimatorComponent>();
        c.defaultState = JGet<std::string>(ja, "defaultState", "");
        c.speed = JGet(ja, "speed", 1.0f);
        c.rootMotion = JGet(ja, "rootMotion", false);
        for (auto& jp : ja.value("params", json::array()))
            c.params.push_back({ JGet<std::string>(jp, "name", ""), EnumFrom(jp, "type", AnimParamType::Float, kParamTypes), JGet(jp, "value", 0.0f) });
        for (auto& js : ja.value("states", json::array())) {
            AnimState s;
            s.name = JGet<std::string>(js, "name", "State");
            s.clip = JGet<std::string>(js, "clip", "");
            s.speed = JGet(js, "speed", 1.0f);
            s.loop = JGet(js, "loop", true);
            s.blendParam = JGet<std::string>(js, "blendParam", "");
            for (auto& jb : js.value("blend", json::array())) s.blend.push_back({ JGet<std::string>(jb, "clip", ""), JGet(jb, "threshold", 0.0f) });
            c.states.push_back(s);
        }
        for (auto& jt : ja.value("transitions", json::array())) {
            AnimTransition tr;
            tr.from = JGet<std::string>(jt, "from", "*");
            tr.to = JGet<std::string>(jt, "to", "");
            tr.duration = JGet(jt, "duration", 0.2f);
            tr.exitTime = JGet(jt, "exitTime", -1.0f);
            for (auto& jc : jt.value("conditions", json::array()))
                tr.conditions.push_back({ JGet<std::string>(jc, "param", ""), EnumFrom(jc, "op", AnimCondOp::Greater, kCondOps), JGet(jc, "threshold", 0.0f) });
            c.transitions.push_back(tr);
        }
    }
    if (j.contains("terrain")) {
        auto& jc = j["terrain"];
        auto& c = e.Add<TerrainComponent>();
        c.resolution = JGet(jc, "resolution", c.resolution);
        c.size = JGet(jc, "size", c.size);
        c.heightScale = JGet(jc, "heightScale", c.heightScale);
        c.seed = JGet(jc, "seed", c.seed);
        c.noiseScale = JGet(jc, "noiseScale", c.noiseScale);
        c.octaves = JGet(jc, "octaves", c.octaves);
        c.sandColor = JGet(jc, "sandColor", c.sandColor);
        c.grassColor = JGet(jc, "grassColor", c.grassColor);
        c.rockColor = JGet(jc, "rockColor", c.rockColor);
        c.snowColor = JGet(jc, "snowColor", c.snowColor);
        c.sandHeight = JGet(jc, "sandHeight", c.sandHeight);
        c.snowHeight = JGet(jc, "snowHeight", c.snowHeight);
        c.grass = JGet(jc, "grass", c.grass);
        c.grassDensity = JGet(jc, "grassDensity", c.grassDensity);
        c.grassMinHeight = JGet(jc, "grassMinHeight", c.grassMinHeight);
        c.treeCount = JGet(jc, "treeCount", c.treeCount);
        c.treeModel = JGet<std::string>(jc, "treeModel", "");
        std::string h = JGet<std::string>(jc, "heights", "");
        if (!h.empty()) {
            auto bytes = Base64Decode(h);
            if (bytes.size() == (size_t)c.resolution * c.resolution * sizeof(float)) {
                c.heights.resize((size_t)c.resolution * c.resolution);
                std::memcpy(c.heights.data(), bytes.data(), bytes.size());
            }
        }
    }
    if (j.contains("water")) {
        auto& jc = j["water"];
        auto& c = e.Add<WaterComponent>();
        c.size = JGet(jc, "size", c.size);
        c.shallowColor = JGet(jc, "shallowColor", c.shallowColor);
        c.deepColor = JGet(jc, "deepColor", c.deepColor);
        c.waveHeight = JGet(jc, "waveHeight", c.waveHeight);
        c.waveSpeed = JGet(jc, "waveSpeed", c.waveSpeed);
        c.waveScale = JGet(jc, "waveScale", c.waveScale);
    }
    if (j.contains("particles")) {
        auto& jc = j["particles"];
        auto& c = e.Add<ParticleEmitterComponent>();
        c.playOnStart = JGet(jc, "playOnStart", c.playOnStart);
        c.playing = c.playOnStart;
        c.loop = JGet(jc, "loop", c.loop);
        c.rate = JGet(jc, "rate", c.rate);
        c.burst = JGet(jc, "burst", c.burst);
        c.maxParticles = JGet(jc, "maxParticles", c.maxParticles);
        c.lifetime = JGet(jc, "lifetime", c.lifetime);
        c.lifetimeVariance = JGet(jc, "lifetimeVariance", c.lifetimeVariance);
        c.shape = EnumFrom(jc, "shape", c.shape, kShapes);
        c.shapeSize = JGet(jc, "shapeSize", c.shapeSize);
        c.velocity = JGet(jc, "velocity", c.velocity);
        c.spread = JGet(jc, "spread", c.spread);
        c.gravity = JGet(jc, "gravity", c.gravity);
        c.drag = JGet(jc, "drag", c.drag);
        c.startColor = JGet(jc, "startColor", c.startColor);
        c.endColor = JGet(jc, "endColor", c.endColor);
        c.startSize = JGet(jc, "startSize", c.startSize);
        c.endSize = JGet(jc, "endSize", c.endSize);
        c.additive = JGet(jc, "additive", c.additive);
        c.worldSpace = JGet(jc, "worldSpace", c.worldSpace);
    }
    if (j.contains("ui")) {
        auto& jc = j["ui"];
        auto& c = e.Add<UIElementComponent>();
        c.type = EnumFrom(jc, "type", c.type, kUITypes);
        c.anchor = EnumFrom(jc, "anchor", c.anchor, kAnchors);
        c.position = JGet(jc, "position", c.position);
        c.size = JGet(jc, "size", c.size);
        c.color = JGet(jc, "color", c.color);
        c.textColor = JGet(jc, "textColor", c.textColor);
        c.text = JGet<std::string>(jc, "text", "");
        c.fontSize = JGet(jc, "fontSize", c.fontSize);
        c.value = JGet(jc, "value", c.value);
        c.image = JGet<std::string>(jc, "image", "");
        c.visible = JGet(jc, "visible", c.visible);
        c.layer = JGet(jc, "layer", c.layer);
    }
    if (j.contains("navAgent")) {
        auto& jc = j["navAgent"];
        auto& c = e.Add<NavAgentComponent>();
        c.speed = JGet(jc, "speed", c.speed);
        c.turnSpeed = JGet(jc, "turnSpeed", c.turnSpeed);
        c.stoppingDistance = JGet(jc, "stoppingDistance", c.stoppingDistance);
        c.repathInterval = JGet(jc, "repathInterval", c.repathInterval);
        c.snapToGround = JGet(jc, "snapToGround", c.snapToGround);
        c.baseOffset = JGet(jc, "baseOffset", c.baseOffset);
    }
    if (j.contains("network")) {
        auto& jc = j["network"];
        auto& c = e.Add<NetworkIdentityComponent>();
        c.owner = JGet<uint32_t>(jc, "owner", 0);
        c.syncTransform = JGet(jc, "syncTransform", true);
        c.interpolate = JGet(jc, "interpolate", true);
    }
    if (j.contains("saveable")) {
        auto& jc = j["saveable"];
        auto& c = e.Add<SaveableComponent>();
        c.saveTransform = JGet(jc, "transform", true);
        c.saveScripts = JGet(jc, "scripts", true);
        c.saveActive = JGet(jc, "active", true);
    }
    return e;
}

json SceneSerializer::ToJson() {
    json j;
    j["scene"] = m_Scene.name;
    j["version"] = 1;
    auto& env = m_Scene.Environment();
    j["environment"] = { { "skyTop", env.skyTop }, { "skyHorizon", env.skyHorizon }, { "groundColor", env.groundColor },
                         { "ambient", env.ambientIntensity }, { "fog", env.fog }, { "fogColor", env.fogColor }, { "fogDensity", env.fogDensity },
                         { "exposure", env.exposure }, { "gravity", env.gravity } };
    auto& ns = m_Scene.Nav().settings;
    j["navigation"] = { { "cellSize", ns.cellSize }, { "agentRadius", ns.agentRadius }, { "agentHeight", ns.agentHeight }, { "maxSlope", ns.maxSlope },
                        { "maxStep", ns.maxStep }, { "minWalkableHeight", ns.minWalkableHeight } };
    j["entities"] = json::array();
    // Parents before children so hierarchy loads in order.
    std::function<void(Entity)> write = [&](Entity e) {
        j["entities"].push_back(SerializeEntity(e));
        for (auto& c : m_Scene.GetChildren(e)) write(c);
    };
    for (auto& e : m_Scene.RootEntities()) write(e);
    return j;
}

bool SceneSerializer::FromJson(const json& j) {
    m_Scene.name = JGet<std::string>(j, "scene", "Untitled");
    if (j.contains("environment")) {
        auto& je = j["environment"];
        auto& env = m_Scene.Environment();
        env.skyTop = JGet(je, "skyTop", env.skyTop);
        env.skyHorizon = JGet(je, "skyHorizon", env.skyHorizon);
        env.groundColor = JGet(je, "groundColor", env.groundColor);
        env.ambientIntensity = JGet(je, "ambient", env.ambientIntensity);
        env.fog = JGet(je, "fog", env.fog);
        env.fogColor = JGet(je, "fogColor", env.fogColor);
        env.fogDensity = JGet(je, "fogDensity", env.fogDensity);
        env.exposure = JGet(je, "exposure", env.exposure);
        env.gravity = JGet(je, "gravity", env.gravity);
    }
    if (j.contains("navigation")) {
        auto& jn = j["navigation"];
        auto& ns = m_Scene.Nav().settings;
        ns.cellSize = JGet(jn, "cellSize", ns.cellSize);
        ns.agentRadius = JGet(jn, "agentRadius", ns.agentRadius);
        ns.agentHeight = JGet(jn, "agentHeight", ns.agentHeight);
        ns.maxSlope = JGet(jn, "maxSlope", ns.maxSlope);
        ns.maxStep = JGet(jn, "maxStep", ns.maxStep);
        ns.minWalkableHeight = JGet(jn, "minWalkableHeight", ns.minWalkableHeight);
    }
    for (auto& je : j.value("entities", json::array())) DeserializeEntity(m_Scene, je);
    return true;
}

bool SceneSerializer::Save(const std::string& path) {
    m_Scene.path = path;
    bool ok = FileSystem::WriteText(Project::Resolve(path), ToJson().dump(1));
    if (ok) ND_INFO("Saved scene {}", path);
    else ND_ERROR("Could not save scene {}", path);
    return ok;
}

bool SceneSerializer::Load(const std::string& path) {
    auto text = FileSystem::ReadText(Project::Resolve(path));
    if (!text) {
        ND_ERROR("Scene not found: {}", path);
        return false;
    }
    try {
        FromJson(json::parse(*text));
    } catch (const std::exception& e) {
        ND_ERROR("Scene {} is invalid: {}", path, e.what());
        return false;
    }
    m_Scene.path = path;
    ND_INFO("Loaded scene {} ({} entities)", path, m_Scene.EntityCount());
    return true;
}

} // namespace nd
