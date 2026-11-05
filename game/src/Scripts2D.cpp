#include "Scripts2D.h"
#include <euphoria/assets/AssetManager.h>
#include <euphoria/renderer/Texture.h>

using namespace eu;

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// The animation called `name` (any case), or "".
std::string FindAnim(SpriteAnimatorComponent& an, const std::string& name) {
    std::string l = Lower(name);
    for (auto& a : an.animations) if (Lower(a.name) == l) return a.name;
    return "";
}
bool HasTag(Entity e, const char* tag) { return e && e.Get<TagComponent>().tag == tag; }

const char* DirName(const vec2& f) {
    if (std::abs(f.x) >= std::abs(f.y)) return f.x < 0.0f ? "left" : "right";
    return f.y < 0.0f ? "down" : "up";
}

// Picks "<base>_<dir>", the mirrored side, or plain "<base>" (mirrored by facing).
bool Choose(SpriteAnimatorComponent& an, const std::string& base, const vec2& facing, bool artFacesLeft, std::string& outName, bool& outFlip) {
    std::string dir = DirName(facing);
    if (auto n = FindAnim(an, base + "_" + dir); !n.empty()) { outName = n; outFlip = false; return true; }
    if (dir == "left" || dir == "right") {
        std::string other = dir == "left" ? "right" : "left";
        if (auto n = FindAnim(an, base + "_" + other); !n.empty()) { outName = n; outFlip = true; return true; }
    }
    if (auto n = FindAnim(an, base); !n.empty()) {
        outName = n;
        outFlip = facing.x < 0.0f ? !artFacesLeft : artFacesLeft;
        return true;
    }
    return false;
}

} // namespace

// ================================================================== PlayerController2D
void PlayerController2D::Reflect(FieldVisitor& v) {
    v.Bool("Top Down", topDown);
    v.Float("Move Speed", moveSpeed);
    v.Float("Acceleration", acceleration);
    v.Float("Air Acceleration", airAcceleration);
    v.Float("Jump Height", jumpHeight);
    v.Float("Coyote Time", coyoteTime, 0.01f);
    v.Float("Jump Buffer", jumpBuffer, 0.01f);
    v.Float("Fall Gravity", fallGravity);
    v.Float("Max Fall Speed", maxFallSpeed);
    v.Float("Kill Y", killY);
    v.String("Idle Animation", idleAnimation);
    v.String("Move Animation", moveAnimation);
    v.String("Jump Animation", jumpAnimation);
    v.String("Fall Animation", fallAnimation);
    v.Bool("Art Faces Left", artFacesLeft);
    v.String("Jump Sound", jumpSound);
    v.String("Hurt Sound", hurtSound);
}

void PlayerController2D::OnCreate() {
    m_Spawn = Transform().position;
    m_Facing = topDown ? vec2(0.0f, -1.0f) : vec2(1.0f, 0.0f);
    if (auto* rb = TryGet<RigidBodyComponent>()) {
        rb->friction = 0.0f;       // no sticking to walls; the controller does its own braking
        rb->restitution = 0.0f;
        if (topDown) rb->gravityScale = 0.0f;
    }
}

void PlayerController2D::OnUpdate(float dt) {
    auto* rb = TryGet<RigidBodyComponent>();
    if (!rb) return;
    vec2 in(Input::GetAxis("MoveX"), Input::GetAxis("MoveY"));
    in = glm::clamp(in, vec2(-1.0f), vec2(1.0f));
    bool grounded = rb->grounded;

    if (topDown) {
        if (glm::length(in) > 1.0f) in = glm::normalize(in);
        vec2 target = in * moveSpeed, v(rb->velocity);
        vec2 d = target - v;
        float step = acceleration * dt;
        v = glm::length(d) <= step ? target : v + glm::normalize(d) * step;
        rb->velocity = vec3(v, 0.0f);
        rb->gravityScale = 0.0f;
        if (glm::length(in) > 0.2f) m_Facing = in;
    } else {
        // Run
        float target = in.x * moveSpeed;
        rb->velocity.x = MoveTowards(rb->velocity.x, target, (grounded ? acceleration : airAcceleration) * dt);
        if (std::abs(in.x) > 0.2f) m_Facing = vec2(in.x > 0.0f ? 1.0f : -1.0f, 0.0f);
        // Jump: coyote time + buffering
        m_Coyote = grounded ? coyoteTime : m_Coyote - dt;
        m_Buffer = Input::GetActionPressed("Jump") ? jumpBuffer : m_Buffer - dt;
        float g = std::abs(GetScene().Environment().gravity.y);
        if (m_Buffer > 0.0f && m_Coyote > 0.0f && g > 0.0f) {
            rb->velocity.y = std::sqrt(2.0f * g * jumpHeight);
            m_Buffer = m_Coyote = 0.0f;
            m_JumpHeld = true;
            grounded = false;
            if (!jumpSound.empty()) Audio::PlayOneShot(jumpSound, 0.5f);
        }
        // Let go early for a short hop
        if (m_JumpHeld && !Input::GetAction("Jump") && rb->velocity.y > 0.0f) { rb->velocity.y *= 0.5f; m_JumpHeld = false; }
        if (rb->velocity.y <= 0.0f) m_JumpHeld = false;
        rb->gravityScale = rb->velocity.y < 0.0f ? fallGravity : 1.0f;
        rb->velocity.y = std::max(rb->velocity.y, -maxFallSpeed);
        if (Transform().position.y < killY) Respawn();
    }
    Animate(*rb, grounded, dt);
}

void PlayerController2D::Animate(RigidBodyComponent& rb, bool grounded, float dt) {
    auto* sr = TryGet<SpriteRendererComponent>();
    if (m_Blink > 0.0f) {
        m_Blink -= dt;
        if (sr) sr->color.a = m_Blink > 0.0f && std::fmod(m_Blink, 0.16f) < 0.08f ? 0.25f : 1.0f;
    }
    auto* an = TryGet<SpriteAnimatorComponent>();
    if (!an || !sr) return;
    std::vector<std::string> wanted;
    float speed = glm::length(vec2(rb.velocity));
    if (topDown || grounded) {
        if ((topDown ? speed : std::abs(rb.velocity.x)) > 0.4f) wanted = { moveAnimation, "run", "walk" };
        else wanted = { idleAnimation, "idle" };
    } else {
        wanted = rb.velocity.y > 0.0f ? std::vector<std::string>{ jumpAnimation, fallAnimation, idleAnimation }
                                      : std::vector<std::string>{ fallAnimation, jumpAnimation, idleAnimation };
    }
    for (auto& w : wanted) {
        std::string name;
        bool flip = false;
        if (w.empty() || !Choose(*an, w, m_Facing, artFacesLeft, name, flip)) continue;
        an->Play(name);
        sr->flipX = flip;
        return;
    }
    // No matching animations: still face the right way
    if (std::abs(m_Facing.x) > 0.0f) sr->flipX = m_Facing.x < 0.0f ? !artFacesLeft : artFacesLeft;
}

void PlayerController2D::Respawn() {
    auto& t = Transform();
    t.position = m_Spawn;
    if (auto* rb = TryGet<RigidBodyComponent>()) { rb->velocity = vec3(0.0f); rb->interpValid = false; }
    m_Blink = 1.2f;
    if (!hurtSound.empty()) Audio::PlayOneShot(hurtSound, 0.6f);
    GetScene().Events().Emit("PlayerTeleported");
}

void PlayerController2D::OnCollisionEnter(Entity other) {
    if (!HasTag(other, "Enemy") || m_Blink > 0.0f) return;
    float myBottom = Transform().position.y;   // the template's characters have their pivot at the feet
    float theirMid = GetScene().WorldPosition(other).y + 0.25f;
    if (!topDown && myBottom > theirMid) {
        // Stomp
        if (auto* rb = TryGet<RigidBodyComponent>()) rb->velocity.y = std::sqrt(2.0f * std::abs(GetScene().Environment().gravity.y) * jumpHeight * 0.55f);
        for (auto& sc : other.Get<ScriptComponent>().scripts)
            if (sc.instance) sc.instance->OnMessage("Stomped");
        return;
    }
    Respawn();
}

// ================================================================== CameraFollow2D
void CameraFollow2D::Reflect(FieldVisitor& v) {
    v.EntityRef("Target", target);
    v.Float("Smooth Time", smoothTime, 0.01f);
    v.Vec3("Offset", offset);
    v.Float("Look Ahead", lookAhead);
    v.Bool("Clamp To Map", clampToMap);
}

Entity CameraFollow2D::Target() {
    if (target) if (Entity e = FindByUUID(target)) return e;
    auto players = GetScene().FindAllWithTag("Player");
    return players.empty() ? Entity() : players.front();
}

vec3 CameraFollow2D::Desired(float dt) {
    Entity t = Target();
    vec3 cur = Transform().position;
    if (!t) return cur;
    vec3 p = GetScene().WorldPosition(t) + offset;
    float vx = 0.0f;
    if (auto* rb = t.TryGet<RigidBodyComponent>()) vx = rb->velocity.x;
    float goal = std::abs(vx) > 0.5f ? (vx > 0.0f ? lookAhead : -lookAhead) : m_Ahead;
    m_Ahead = MoveTowards(m_Ahead, goal, lookAhead * 1.5f * dt);
    p.x += m_Ahead;
    p.z = cur.z;
    return p;
}

void CameraFollow2D::Clamp(vec3& p) {
    if (!clampToMap) return;
    Scene& scene = GetScene();
    const auto& env = scene.Environment();
    float ppu = std::max(1.0f, env.pixelsPerUnit);
    bool found = false;
    vec2 mn, mx;
    scene.Reg().View<TransformComponent, TilemapComponent>([&](EntityId id, TransformComponent&, TilemapComponent& tc) {
        if (found) return;
        TilemapData* map = EnsureTilemapLoaded(tc);
        if (!map) return;
        mat4 w = scene.WorldMatrix(Entity(id, &scene));
        vec2 a = vec2(w * vec4(0, 0, 0, 1));
        vec2 b = vec2(w * vec4(map->bounds.z * (float)map->tileWidth / ppu, map->PixelHeight() / ppu - map->bounds.y * (float)map->tileHeight / ppu, 0, 1));
        mn = glm::min(a, b);
        mx = glm::max(a, b);
        found = true;
    });
    if (!found) return;
    Renderer2D::View view = Renderer2D::LastGameView();
    vec2 half;
    if (view.lowRes) half = vec2(view.lowWidth, view.lowHeight) / ppu * 0.5f;
    else if (view.lowHeight > 0) half = vec2(1.0f / view.camera.projection[0][0], 1.0f / view.camera.projection[1][1]);
    else half = vec2(env.referenceResolution) / ppu * 0.5f;
    for (int i = 0; i < 2; i++) {
        if (mx[i] - mn[i] <= half[i] * 2.0f) p[i] = (mn[i] + mx[i]) * 0.5f;
        else p[i] = glm::clamp(p[i], mn[i] + half[i], mx[i] - half[i]);
    }
}

void CameraFollow2D::OnStart() {
    vec3 p = Desired(0.0f);
    Clamp(p);
    Transform().position = p;
    On("PlayerTeleported", [this](const eu::Event&) {
        m_Ahead = 0.0f;
        vec3 p = Desired(0.0f);
        Clamp(p);
        Transform().position = p;
    });
}

void CameraFollow2D::OnLateUpdate(float dt) {
    vec3 goal = Desired(dt);
    Clamp(goal);
    vec3& p = Transform().position;
    float k = smoothTime > 0.0f ? 1.0f - std::exp(-dt / smoothTime) : 1.0f;
    p = glm::mix(p, goal, k);
    Clamp(p);
}

// ================================================================== Collectible2D
void Collectible2D::Reflect(FieldVisitor& v) {
    v.Int("Value", value);
    v.String("Sound", sound);
    v.Float("Bob", bob, 0.01f);
}

void Collectible2D::OnCreate() {
    m_Base = Transform().position;
    m_Time = (float)(GetEntity().GetUUID() % 1000) * 0.01f;   // don't bob in sync
}

void Collectible2D::OnUpdate(float dt) {
    m_Time += dt;
    Transform().position = m_Base + vec3(0.0f, std::sin(m_Time * 3.0f) * bob, 0.0f);
}

void Collectible2D::OnTriggerEnter(Entity other) {
    if (m_Taken || !HasTag(other, "Player")) return;
    m_Taken = true;
    if (!sound.empty()) Audio::PlayOneShot(sound, 0.55f);
    Emit("Collected", { { "value", value } });
    DestroySelf();
}

// ================================================================== Patrol2D
void Patrol2D::Reflect(FieldVisitor& v) {
    v.Float("Speed", speed);
    v.Bool("Turn At Ledges", turnAtLedges);
    v.String("Die Sound", dieSound);
}

void Patrol2D::OnCreate() {
    if (auto* rb = TryGet<RigidBodyComponent>()) { rb->friction = 0.0f; rb->restitution = 0.0f; }
}

void Patrol2D::OnUpdate(float dt) {
    auto* rb = TryGet<RigidBodyComponent>();
    auto* sr = TryGet<SpriteRendererComponent>();
    if (m_Dying >= 0.0f) {
        m_Dying += dt;
        Transform().scale.y = std::max(0.2f, 1.0f - m_Dying * 4.0f);
        if (sr) sr->color.a = std::max(0.0f, 1.0f - m_Dying * 2.0f);
        if (m_Dying > 0.5f) DestroySelf();
        return;
    }
    if (!rb) return;
    Scene& scene = GetScene();
    vec3 p = Transform().position;
    vec3 half(0.4f);
    if (auto* box = TryGet<BoxColliderComponent>()) half = box->size * Transform().scale * 0.5f;
    PhysicsWorld& phys = scene.Physics();
    RaycastHit hit;
    vec3 mid = p + vec3(0.0f, half.y, 0.0f);   // pivot at the feet
    if (auto* box = TryGet<BoxColliderComponent>()) mid = p + box->offset * Transform().scale;
    bool wall = phys.Raycast({ mid, vec3(m_Dir, 0, 0) }, half.x + 0.08f, hit, GetEntity().Id()) && !HasTag(hit.entity, "Player");
    bool ledge = false;
    if (turnAtLedges && rb->grounded) {
        vec3 ahead = mid + vec3(m_Dir * (half.x + 0.1f), 0, 0);
        ledge = !phys.Raycast({ ahead, vec3(0, -1, 0) }, half.y + 0.4f, hit, GetEntity().Id());
    }
    if (wall || ledge) m_Dir = -m_Dir;
    rb->velocity.x = m_Dir * speed;
    if (sr) sr->flipX = m_Dir < 0.0f;
}

void Patrol2D::OnMessage(const std::string& message) {
    if (message != "Stomped" || m_Dying >= 0.0f) return;
    m_Dying = 0.0f;
    if (!dieSound.empty()) Audio::PlayOneShot(dieSound, 0.6f);
    GetEntity().Get<TagComponent>().tag = "Untagged";
    if (auto* rb = TryGet<RigidBodyComponent>()) rb->velocity = vec3(0.0f);
    if (auto* box = TryGet<BoxColliderComponent>()) box->isTrigger = true;
    Emit("EnemyDefeated");
}

// ================================================================== Hud2D
void Hud2D::Reflect(FieldVisitor& v) {
    v.String("Title", title);
    v.String("Item Name", itemName);
    v.String("Icon", icon);
    v.String("Controls", controls);
}

void Hud2D::OnStart() {
    m_Total = 0;
    GetScene().Reg().View<ScriptComponent>([&](EntityId, ScriptComponent& sc) {
        for (auto& s : sc.scripts) if (dynamic_cast<Collectible2D*>(s.instance.get())) m_Total += dynamic_cast<Collectible2D*>(s.instance.get())->value;
    });
    On("Collected", [this](const eu::Event& e) {
        m_Collected += e.data.value("value", 1);
        if (m_Total > 0 && m_Collected >= m_Total && m_DoneTime < 0.0f) m_DoneTime = m_Time;
    });
}

void Hud2D::OnGUI() {
    vec2 canvas = UI::CanvasSize();
    if (m_Total > 0) {
        UI::Rect box = UI::Anchored(UIAnchor::TopLeft, { 28, 28 }, { 280, 84 });
        UI::PanelBordered(box, { 0.05f, 0.04f, 0.08f, 0.6f }, { 1, 1, 1, 0.15f });
        float x = box.pos.x + 20;
        if (!icon.empty()) {
            if (auto tex = AssetManager::GetTexture(icon, true)) {
                // The first frame of a sprite sheet (the whole image if it isn't one)
                ivec2 frame = SpriteSheet::DetectFrameSize(icon);
                if (frame.x <= 0 || frame.y <= 0) frame = { tex->Width(), tex->Height() };
                float h = 52.0f, w = h * frame.x / std::max(1, frame.y);
                UI::ImageRegion({ { x, box.pos.y + 16 }, { w, h } }, tex, vec4(0, 0, frame.x, frame.y));
                x += w + 16;
            }
        }
        UI::Text({ x, box.pos.y + 10 }, itemName, 22, { 1, 1, 1, 0.7f });
        UI::Text({ x, box.pos.y + 36 }, std::to_string(m_Collected) + " / " + std::to_string(m_Total), 40, { 1.0f, 0.85f, 0.35f, 1 });
    }
    if (m_Time < 4.0f) {
        float a = m_Time < 3.0f ? 1.0f : 4.0f - m_Time;
        UI::Text({ canvas.x * 0.5f + 4, 164 }, title, 80, { 0, 0, 0, a * 0.5f }, UI::Align::Center);
        UI::Text({ canvas.x * 0.5f, 160 }, title, 80, { 1, 1, 1, a }, UI::Align::Center);
        UI::Text({ canvas.x * 0.5f, canvas.y - 90 }, controls, 28, { 1, 1, 1, a * 0.85f }, UI::Align::Center);
    }
    if (m_DoneTime >= 0.0f && m_Time - m_DoneTime < 5.0f) {
        float a = std::min(1.0f, (5.0f - (m_Time - m_DoneTime)));
        UI::Text({ canvas.x * 0.5f + 3, 263 }, "You found them all!", 64, { 0, 0, 0, a * 0.5f }, UI::Align::Center);
        UI::Text({ canvas.x * 0.5f, 260 }, "You found them all!", 64, { 1.0f, 0.88f, 0.4f, a }, UI::Align::Center);
    }
}

EU_SCRIPT(PlayerController2D)
EU_SCRIPT(CameraFollow2D)
EU_SCRIPT(Collectible2D)
EU_SCRIPT(Patrol2D)
EU_SCRIPT(Hud2D)
