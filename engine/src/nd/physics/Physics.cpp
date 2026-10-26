#include "nd/physics/Physics.h"
#include "nd/renderer/DebugDraw.h"
#include "nd/core/Profiler.h"
#include <cmath>

namespace nd {

static vec3 BoxWorldExtents(const vec3& half, const quat& rot) {
    mat3 r = glm::toMat3(rot);
    vec3 e(0.0f);
    for (int i = 0; i < 3; i++) e += glm::abs(r[i]) * half[i];
    return e;
}

void PhysicsWorld::ComputeBounds(Body& b) {
    vec3 e = b.sphere ? vec3(b.radius) : BoxWorldExtents(b.half, b.rot);
    b.bounds.min = b.center - e;
    b.bounds.max = b.center + e;
}

void PhysicsWorld::Gather() {
    m_Bodies.clear();
    Registry& reg = m_Scene->Reg();
    auto add = [&](EntityId id, bool sphere) {
        Entity e(id, m_Scene);
        if (!m_Scene->IsActiveInHierarchy(e)) return;
        auto& tf = reg.Get<TransformComponent>(id);
        mat4 world = m_Scene->WorldMatrix(e);
        vec3 t, s; quat r;
        DecomposeTRS(world, t, r, s);
        Body b{};
        b.id = id;
        b.sphere = sphere;
        b.tf = &tf;
        b.rb = reg.TryGet<RigidBodyComponent>(id);
        b.rot = r;
        if (sphere) {
            auto& c = reg.Get<SphereColliderComponent>(id);
            b.radius = c.radius * std::max(s.x, std::max(s.y, s.z));
            b.center = vec3(world * vec4(c.offset, 1.0f));
            b.trigger = c.isTrigger;
        } else {
            auto& c = reg.Get<BoxColliderComponent>(id);
            b.half = c.size * s * 0.5f;
            b.center = vec3(world * vec4(c.offset, 1.0f));
            b.trigger = c.isTrigger;
        }
        b.offsetWorld = b.center - t;
        b.movable = b.rb && b.rb->type == BodyType::Dynamic && tf.parent == 0;
        b.invMass = b.movable && b.rb->mass > 0.0f ? 1.0f / b.rb->mass : 0.0f;
        m_Bodies.push_back(b);
    };
    reg.View<TransformComponent, SphereColliderComponent>([&](EntityId id, TransformComponent&, SphereColliderComponent&) { add(id, true); });
    reg.View<TransformComponent, BoxColliderComponent>([&](EntityId id, TransformComponent&, BoxColliderComponent&) {
        if (!reg.Has<SphereColliderComponent>(id)) add(id, false);
    });
    m_BodyCount = (uint32_t)m_Bodies.size();
}

bool PhysicsWorld::TerrainHeight(const vec3& p, float& height, vec3* normal) {
    bool found = false;
    m_Scene->Reg().View<TransformComponent, TerrainComponent>([&](EntityId id, TransformComponent&, TerrainComponent& t) {
        if (found || t.heights.empty()) return;
        Entity e(id, m_Scene);
        if (!m_Scene->IsActiveInHierarchy(e)) return;
        mat4 world = m_Scene->WorldMatrix(e);
        vec3 local = vec3(glm::inverse(world) * vec4(p, 1.0f));
        float half = t.size * 0.5f;
        if (local.x < -half || local.x > half || local.z < -half || local.z > half) return;
        vec3 hp = vec3(world * vec4(local.x, t.HeightAtLocal(local.x, local.z), local.z, 1.0f));
        height = hp.y;
        if (normal) *normal = glm::normalize(mat3(world) * t.NormalAtLocal(local.x, local.z));
        found = true;
    });
    return found;
}

void PhysicsWorld::CollideTerrain(Body& b) {
    if (!b.movable && !(b.rb && b.rb->type == BodyType::Kinematic)) return;
    float h;
    vec3 n;
    if (!TerrainHeight(b.center, h, &n)) return;
    float extent = b.sphere ? b.radius : BoxWorldExtents(b.half, b.rot).y;
    float bottom = b.center.y - extent;
    if (bottom >= h) return;
    if (b.trigger || !b.movable) return;
    float depth = h - bottom;
    // Push out along the vertical (stable on slopes for characters).
    b.center.y += depth;
    b.tf->position.y += depth;
    vec3& v = b.rb->velocity;
    float vn = glm::dot(v, n);
    if (vn < 0.0f) {
        v -= (1.0f + b.rb->restitution) * vn * n;
        vec3 tangent = v - glm::dot(v, n) * n;
        float tl = glm::length(tangent);
        if (tl > 1e-5f) {
            float drop = std::min(tl, -vn * b.rb->friction);
            v -= tangent / tl * drop;
        }
    }
    if (n.y > 0.6f) { b.rb->grounded = true; b.rb->groundNormal = n; }
}

bool PhysicsWorld::Collide(const Body& a, const Body& b, vec3& normal, float& depth) {
    // normal points from a to b
    if (a.sphere && b.sphere) {
        vec3 d = b.center - a.center;
        float dist2 = glm::dot(d, d), r = a.radius + b.radius;
        if (dist2 >= r * r) return false;
        float dist = std::sqrt(dist2);
        normal = dist > 1e-6f ? d / dist : vec3(0, 1, 0);
        depth = r - dist;
        return true;
    }
    if (a.sphere != b.sphere) {
        const Body& s = a.sphere ? a : b;
        const Body& box = a.sphere ? b : a;
        quat inv = glm::inverse(box.rot);
        vec3 local = inv * (s.center - box.center);
        vec3 closest = glm::clamp(local, -box.half, box.half);
        vec3 diff = local - closest;
        float dist2 = glm::dot(diff, diff);
        vec3 nLocal;
        if (dist2 > 1e-10f) {
            if (dist2 >= s.radius * s.radius) return false;
            float dist = std::sqrt(dist2);
            nLocal = diff / dist;                 // box -> sphere
            depth = s.radius - dist;
        } else {
            // sphere center inside box: push out along the closest face
            vec3 dists = box.half - glm::abs(local);
            int axis = dists.x < dists.y ? (dists.x < dists.z ? 0 : 2) : (dists.y < dists.z ? 1 : 2);
            nLocal = vec3(0.0f);
            nLocal[axis] = local[axis] >= 0 ? 1.0f : -1.0f;
            depth = dists[axis] + s.radius;
        }
        vec3 n = box.rot * nLocal;                // box -> sphere
        normal = a.sphere ? -n : n;
        return true;
    }
    // box vs box (world AABBs)
    vec3 ea = BoxWorldExtents(a.half, a.rot), eb = BoxWorldExtents(b.half, b.rot);
    vec3 d = b.center - a.center;
    vec3 overlap = ea + eb - glm::abs(d);
    if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0) return false;
    int axis = overlap.x < overlap.y ? (overlap.x < overlap.z ? 0 : 2) : (overlap.y < overlap.z ? 1 : 2);
    normal = vec3(0.0f);
    normal[axis] = d[axis] >= 0 ? 1.0f : -1.0f;
    depth = overlap[axis];
    return true;
}

void PhysicsWorld::Resolve(Body& a, Body& b, const vec3& n, float depth) {
    float invSum = a.invMass + b.invMass;
    bool aKin = a.rb && a.rb->type == BodyType::Kinematic;
    bool bKin = b.rb && b.rb->type == BodyType::Kinematic;
    if (invSum <= 0.0f) return;

    // Positional correction (split by inverse mass)
    const float slop = 0.002f;
    float corr = std::max(depth - slop, 0.0f) / invSum;
    vec3 move = n * corr;
    if (a.movable) { a.tf->position -= move * a.invMass; a.center -= move * a.invMass; ComputeBounds(a); }
    if (b.movable) { b.tf->position += move * b.invMass; b.center += move * b.invMass; ComputeBounds(b); }

    // Grounding (n points a -> b)
    if (n.y > 0.6f && b.rb) { b.rb->grounded = true; b.rb->groundNormal = n; }
    if (n.y < -0.6f && a.rb) { a.rb->grounded = true; a.rb->groundNormal = -n; }

    vec3 va = a.rb && (a.movable || aKin) ? a.rb->velocity : vec3(0.0f);
    vec3 vb = b.rb && (b.movable || bKin) ? b.rb->velocity : vec3(0.0f);
    vec3 rv = vb - va;
    float vn = glm::dot(rv, n);
    if (vn > 0.0f) return;
    float e = std::min(a.rb ? a.rb->restitution : 0.2f, b.rb ? b.rb->restitution : 0.2f);
    float j = -(1.0f + e) * vn / invSum;
    vec3 impulse = j * n;
    if (a.movable) a.rb->velocity -= impulse * a.invMass;
    if (b.movable) b.rb->velocity += impulse * b.invMass;

    // Coulomb friction
    vec3 tangent = rv - vn * n;
    float tl = glm::length(tangent);
    if (tl > 1e-5f) {
        tangent /= tl;
        float jt = -glm::dot(rv, tangent) / invSum;
        float mu = std::sqrt((a.rb ? a.rb->friction : 0.6f) * (b.rb ? b.rb->friction : 0.6f));
        jt = glm::clamp(jt, -j * mu, j * mu);
        vec3 ft = jt * tangent;
        if (a.movable) a.rb->velocity -= ft * a.invMass;
        if (b.movable) b.rb->velocity += ft * b.invMass;
    }
}

void PhysicsWorld::Step(float dt) {
    ND_PROFILE_SCOPE("Physics");
    Gather();
    const vec3 gravity = m_Scene->Environment().gravity;

    // Integrate
    for (auto& b : m_Bodies) {
        if (!b.rb) continue;
        b.rb->grounded = false;
        if (b.movable) {
            RigidBodyComponent& rb = *b.rb;
            vec3 acc = gravity * rb.gravityScale + rb.force * b.invMass;
            rb.velocity += acc * dt;
            rb.velocity *= 1.0f / (1.0f + rb.drag * dt);
            if (rb.lockY) rb.velocity.y = 0.0f;
            b.tf->position += rb.velocity * dt;
            b.center += rb.velocity * dt;
            rb.force = vec3(0.0f);
        } else if (b.rb->type == BodyType::Kinematic && b.tf->parent == 0) {
            b.tf->position += b.rb->velocity * dt;
            b.center += b.rb->velocity * dt;
        }
        ComputeBounds(b);
    }
    for (auto& b : m_Bodies) ComputeBounds(b);

    std::set<std::pair<EntityId, EntityId>> contacts, triggers;

    // Sort-and-sweep broadphase + iterative resolution
    std::vector<int> order(m_Bodies.size());
    for (int it = 0; it < solverIterations; it++) {
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int x, int y) { return m_Bodies[x].bounds.min.x < m_Bodies[y].bounds.min.x; });
        for (size_t i = 0; i < order.size(); i++) {
            Body& a = m_Bodies[order[i]];
            for (size_t k = i + 1; k < order.size(); k++) {
                Body& b = m_Bodies[order[k]];
                if (b.bounds.min.x > a.bounds.max.x) break;
                if (!a.movable && !b.movable && !(a.trigger || b.trigger)) continue;
                if (a.bounds.min.y > b.bounds.max.y || a.bounds.max.y < b.bounds.min.y ||
                    a.bounds.min.z > b.bounds.max.z || a.bounds.max.z < b.bounds.min.z) continue;
                vec3 n; float depth;
                if (!Collide(a, b, n, depth)) continue;
                auto key = std::minmax(a.id, b.id);
                if (a.trigger || b.trigger) {
                    // static triggers only detect moving things
                    if (a.movable || b.movable || (a.rb && a.rb->type == BodyType::Kinematic) || (b.rb && b.rb->type == BodyType::Kinematic))
                        triggers.insert(key);
                    continue;
                }
                contacts.insert(key);
                Resolve(a, b, n, depth);
            }
        }
        for (auto& b : m_Bodies) CollideTerrain(b);
    }

    // Enter / exit events
    for (auto& c : contacts) if (!m_Contacts.count(c)) m_Events.push_back({ CollisionEvent::CollisionEnter, c.first, c.second });
    for (auto& c : m_Contacts) if (!contacts.count(c)) m_Events.push_back({ CollisionEvent::CollisionExit, c.first, c.second });
    for (auto& c : triggers) if (!m_TriggerContacts.count(c)) m_Events.push_back({ CollisionEvent::TriggerEnter, c.first, c.second });
    for (auto& c : m_TriggerContacts) if (!triggers.count(c)) m_Events.push_back({ CollisionEvent::TriggerExit, c.first, c.second });
    m_Contacts = std::move(contacts);
    m_TriggerContacts = std::move(triggers);
}

static bool RaySphere(const Ray& r, const vec3& c, float rad, float& t) {
    vec3 oc = r.origin - c;
    float b = glm::dot(oc, r.direction);
    float cc = glm::dot(oc, oc) - rad * rad;
    float disc = b * b - cc;
    if (disc < 0) return false;
    float s = std::sqrt(disc);
    t = -b - s;
    if (t < 0) t = -b + s;
    return t >= 0;
}

static bool RayOBB(const Ray& r, const vec3& c, const vec3& half, const quat& rot, float& t, vec3& normal) {
    quat inv = glm::inverse(rot);
    vec3 o = inv * (r.origin - c), d = inv * r.direction;
    float tmin = -1e30f, tmax = 1e30f;
    int axisMin = 0;
    float signMin = 1.0f;
    for (int i = 0; i < 3; i++) {
        if (std::abs(d[i]) < 1e-8f) {
            if (o[i] < -half[i] || o[i] > half[i]) return false;
            continue;
        }
        float t1 = (-half[i] - o[i]) / d[i], t2 = (half[i] - o[i]) / d[i];
        float s = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
        if (t1 > tmin) { tmin = t1; axisMin = i; signMin = s; }
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmax < 0) return false;
    t = tmin >= 0 ? tmin : tmax;
    vec3 n(0.0f);
    n[axisMin] = signMin;
    normal = rot * n;
    return true;
}

bool PhysicsWorld::Raycast(const Ray& rayIn, float maxDistance, RaycastHit& hit, EntityId ignore, bool includeTriggers) {
    Ray ray = rayIn;
    ray.direction = glm::normalize(ray.direction);
    if (!m_Batch) Gather();
    float best = maxDistance;
    bool found = false;
    for (auto& b : m_Bodies) {
        if (b.id == ignore || (b.trigger && !includeTriggers)) continue;
        float t = 0.0f;
        vec3 n(0.0f, 1.0f, 0.0f);
        bool h = b.sphere ? RaySphere(ray, b.center, b.radius, t) : RayOBB(ray, b.center, b.half, b.rot, t, n);
        if (h && t < best) {
            best = t;
            found = true;
            hit.entity = Entity(b.id, m_Scene);
            hit.hitTerrain = false;
            hit.point = ray.origin + ray.direction * t;
            hit.normal = b.sphere ? glm::normalize(hit.point - b.center) : n;
            hit.distance = t;
        }
    }
    float h;
    // Straight-down rays (ground checks, nav baking): sample the heightfield directly.
    if (ray.direction.y < -0.9999f) {
        if (TerrainHeight(ray.origin, h) && ray.origin.y >= h && ray.origin.y - h < best) {
            vec3 n(0, 1, 0);
            TerrainHeight(ray.origin, h, &n);
            hit.entity = Entity();
            hit.hitTerrain = true;
            hit.point = vec3(ray.origin.x, h, ray.origin.z);
            hit.normal = n;
            hit.distance = ray.origin.y - h;
            return true;
        }
        return found;
    }
    // Terrain: march then refine
    float step = 0.5f, prevT = 0.0f;
    bool prevAbove = true;
    for (float t = 0.0f; t < best; t += step) {
        vec3 p = ray.origin + ray.direction * t;
        if (!TerrainHeight(p, h)) { prevAbove = true; prevT = t; continue; }
        bool above = p.y > h;
        if (!above && prevAbove && t > 0.0f) {
            float lo = prevT, hi = t;
            for (int i = 0; i < 12; i++) {
                float mid = (lo + hi) * 0.5f;
                vec3 mp = ray.origin + ray.direction * mid;
                float mh;
                if (TerrainHeight(mp, mh) && mp.y <= mh) hi = mid; else lo = mid;
            }
            if (hi < best) {
                best = hi;
                found = true;
                hit.entity = Entity();
                hit.hitTerrain = true;
                hit.point = ray.origin + ray.direction * hi;
                vec3 n(0, 1, 0);
                TerrainHeight(hit.point, h, &n);
                hit.normal = n;
                hit.distance = hi;
            }
            break;
        }
        prevAbove = above;
        prevT = t;
    }
    return found;
}

std::vector<Entity> PhysicsWorld::OverlapSphere(const vec3& center, float radius, bool includeTriggers) {
    if (!m_Batch) Gather();
    std::vector<Entity> out;
    Body probe{};
    probe.sphere = true;
    probe.center = center;
    probe.radius = radius;
    for (auto& b : m_Bodies) {
        if (b.trigger && !includeTriggers) continue;
        vec3 n; float d;
        if (Collide(probe, b, n, d)) out.push_back(Entity(b.id, m_Scene));
    }
    return out;
}

void PhysicsWorld::DebugDraw() {
    Gather();
    for (auto& b : m_Bodies) {
        vec4 col = b.trigger ? vec4(1.0f, 0.8f, 0.2f, 1.0f) : b.movable ? vec4(0.3f, 1.0f, 0.4f, 1.0f) : vec4(0.3f, 0.7f, 1.0f, 1.0f);
        if (b.sphere) DebugDraw::Sphere(b.center, b.radius, col);
        else DebugDraw::Box(b.center, b.half, b.rot, col);
    }
}

} // namespace nd
