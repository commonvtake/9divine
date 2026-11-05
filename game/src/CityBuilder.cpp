// Divine City: a dusk city block (City.scene) with enterable buildings - the Noodle Bar and
// Apartment 9 interiors (their own scenes), the hotel roof (same scene) and the subway to
// Ember Isle. Every door is a Door script: fade to black, move the player, fade back in.
//
// Layout (metres, +X east, +Z south): Main Street runs east-west and Cross Street north-south,
// meeting at the origin. Roads are 10 m wide with 3.5 m sidewalks, so building fronts sit
// 8.5 m from each street's centre line. Each quadrant has a corner building plus a row of
// buildings along each street; tall buildings close off all four street ends.
#include "DemoKit.h"
#include <euphoria/scene/SceneSerializer.h>
#include <cmath>

using namespace eu;

namespace demo {
namespace {

constexpr float kFront = 8.5f;     // street centre line -> building front
constexpr float kRowEnd = 72.0f;   // where the street-end buildings start
constexpr float kWalk = 0.1f;      // sidewalk height

struct Rng {
    uint32_t s;
    float operator()() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }
    int Int(int n) { return std::min(n - 1, (int)((*this)() * n)); }
};

// A building front: origin is on the front face at ground level, n points out to the street
// and t runs along the front. Boxes placed with At() and rotated by yaw use local sizes
// (along the front, height, thickness), whatever direction the front faces.
struct Front {
    vec3 origin, n, t;
    float yaw;
    vec3 At(float u, float y, float out) const { return origin + t * u + n * out + vec3(0, y, 0); }
};

Front MakeFront(const vec3& origin, const vec3& n) {
    Front f;
    f.origin = origin;
    f.n = n;
    f.t = vec3(n.z, 0.0f, -n.x);                         // local +X of a yaw that maps +Z to n
    f.yaw = glm::degrees(std::atan2(n.x, n.z));
    return f;
}

Entity Decor(Scene& s, const std::string& name, const vec3& pos, const vec3& size, const vec4& color, const std::string& tex = "",
             const vec3& euler = vec3(0.0f)) {
    Entity e = Box(s, name, pos, size, color, tex, 1.0f, BodyType::Static, euler);
    e.Remove<BoxColliderComponent>();
    return e;
}

Entity FrontBox(Scene& s, const std::string& name, const Front& f, float u, float y, const vec3& size, float out, const vec4& color,
                bool collider = false, const std::string& tex = "") {
    Entity e = Box(s, name, f.At(u, y, out), size, color, tex, 1.0f, BodyType::Static, { 0, f.yaw, 0 });
    if (!collider) e.Remove<BoxColliderComponent>();
    return e;
}

MeshRendererComponent& Mat(Entity e) { return e.Get<MeshRendererComponent>(); }

Entity Glow(Entity e, const vec3& emissive) {
    Mat(e).emissive = emissive;
    Mat(e).castShadows = false;
    return e;
}

Entity Sign(Scene& s, const Front& f, float u, float y, float width, const std::string& texture, float out = 0.1f) {
    Entity e = FrontBox(s, "Sign", f, u, y, { width, width * 0.3125f, 0.12f }, out, vec4(1.0f), false, texture);
    Mat(e).emissiveMap = texture;
    Glow(e, vec3(2.4f));
    return e;
}

Entity PointLight(Scene& s, const std::string& name, const vec3& pos, const vec3& color, float intensity, float range) {
    Entity e = s.CreateEntity(name);
    e.Transform().position = pos;
    auto& l = e.Add<LightComponent>();
    l.type = LightType::Point;
    l.color = color;
    l.intensity = intensity;
    l.range = range;
    return e;
}

// Somewhere for a door to put the player: just outside the door, facing away from it.
Entity SpawnPoint(Scene& s, const std::string& name, const vec3& pos, const vec3& facing) {
    Entity e = s.CreateEntity(name);
    e.Transform().position = pos;
    e.Transform().SetEulerDegrees({ 0, glm::degrees(std::atan2(-facing.x, -facing.z)), 0 });
    e.Get<TagComponent>().tag = "SpawnPoint";
    return e;
}

// A door in a front at offset u: frame, slab with the Door script, handle, optional lamp,
// and a spawn point named "Spawn - <spawnName>" in front of it for doors that lead here.
Entity Doorway(Scene& s, const Front& f, float u, const std::string& label, const vec4& color, json doorFields,
               const std::string& spawnName, bool lamp) {
    FrontBox(s, "Door Frame", f, u, 1.27f, { 1.62f, 2.62f, 0.12f }, 0.06f, { 0.78f, 0.76f, 0.72f, 1 });
    Entity door = FrontBox(s, "Door - " + label, f, u, 1.2f, { 1.3f, 2.4f, 0.1f }, 0.1f, color, true);
    Mat(door).roughness = 0.45f;
    Entity knob = Mesh(s, "Handle", "builtin:sphere", f.At(u + 0.45f, 1.05f, 0.19f), vec3(0.09f), { 0.85f, 0.7f, 0.4f, 1 });
    Mat(knob).metallic = 1.0f;
    Mat(knob).roughness = 0.3f;
    if (lamp) Glow(FrontBox(s, "Door Lamp", f, u, 2.8f, { 0.36f, 0.14f, 0.22f }, 0.11f, { 1, 0.9f, 0.7f, 1 }), { 3.0f, 2.2f, 1.2f });
    doorFields["Label"] = label;
    doorFields["Offset"] = vec3(0.0f, -0.7f, 0.6f);   // in front of the slab, at hand height
    AddScript(door, "Door", doorFields);
    if (!spawnName.empty()) SpawnPoint(s, "Spawn - " + spawnName, f.At(u, 0.25f, 1.3f), f.n);
    return door;
}

void AddExamine(Entity e, const std::string& label, const std::string& message, float range = 1.8f, const vec3& offset = vec3(0.0f),
                json extra = json::object()) {
    extra["Label"] = label;
    extra["Message"] = message;
    extra["Range"] = range;
    extra["Offset"] = offset;
    AddScript(e, "Examine", extra);
}

// ------------------------------------------------------------------ buildings
struct Shop {
    std::string label;          // door prompt
    std::string sign;           // sign texture ("" = awning instead)
    json door;                  // Door fields
    std::string spawn;          // spawn point name in front of the door ("" = none)
    vec3 window{ 1.0f, 0.75f, 0.45f };   // shop window glow
    std::string interior = "Textures/window_shelves.png";   // what's behind the glass
    bool lamp = false;
};

const char* kFacades[] = { "Textures/facade_brick.png", "Textures/facade_concrete.png", "Textures/facade_tile.png" };
const vec4 kFacadeTints[3][2] = {
    { { 1.0f, 1.0f, 1.0f, 1 }, { 0.82f, 0.86f, 0.95f, 1 } },
    { { 1.0f, 1.0f, 1.0f, 1 }, { 0.92f, 0.86f, 0.8f, 1 } },
    { { 1.0f, 1.0f, 1.0f, 1 }, { 0.95f, 0.8f, 0.85f, 1 } },
};
const vec4 kPlinth[] = { { 0.20f, 0.19f, 0.22f, 1 }, { 0.30f, 0.25f, 0.21f, 1 }, { 0.17f, 0.20f, 0.22f, 1 } };
const vec4 kDoorColors[] = { { 0.32f, 0.09f, 0.08f, 1 }, { 0.10f, 0.14f, 0.26f, 1 }, { 0.11f, 0.22f, 0.15f, 1 }, { 0.08f, 0.08f, 0.09f, 1 } };
const vec4 kAwnings[] = { { 0.62f, 0.12f, 0.12f, 1 }, { 0.12f, 0.35f, 0.32f, 1 }, { 0.75f, 0.55f, 0.15f, 1 }, { 0.2f, 0.22f, 0.45f, 1 } };
const vec3 kWindowGlow[] = { { 1.0f, 0.72f, 0.42f }, { 0.55f, 0.75f, 1.0f }, { 1.0f, 0.45f, 0.65f }, { 0.9f, 0.9f, 0.75f } };
const char* kLockedLines[] = {
    "It's locked.",
    "Locked. You can hear a TV through the door.",
    "The handle is ice cold. It won't turn.",
    "A note on the door: BACK IN 5 MINUTES.\nThe note is yellow with age.",
    "Locked. Someone inside tells you to go away.",
    "It's barred from the inside.",
    "Locked. There's a smell of fresh paint.",
};

// A building with its front on a street: shop floor (door, window, sign or awning), then the
// textured upper floors, a ledge between them, and a cornice that doubles as the roof.
// Returns the front so callers can add more to it.
Front Building(Scene& s, Rng& rnd, const std::string& name, const Front& f, float width, float depth, float height, const Shop* shop,
               bool rooftop = true) {
    vec3 c = f.origin - f.n * (depth * 0.5f);
    const vec3 rot(0, f.yaw, 0);
    int style = rnd.Int(3);
    Entity ground = Box(s, name + " Ground Floor", c + vec3(0, 2.0f, 0), { width, 4.0f, depth }, kPlinth[rnd.Int(3)], "", 1.0f, BodyType::Static, rot);
    Mat(ground).roughness = 0.85f;
    Box(s, name, c + vec3(0, 4.0f + (height - 4.0f) * 0.5f, 0), { width, height - 4.0f, depth }, kFacadeTints[style][rnd.Int(2)], kFacades[style],
        1.0f / 16.0f, BodyType::Static, rot);
    Decor(s, "Ledge", c + vec3(0, 4.0f, 0), { width + 0.3f, 0.25f, depth + 0.3f }, { 0.7f, 0.68f, 0.64f, 1 }, "", rot);
    Box(s, "Roof", c + vec3(0, height + 0.15f, 0), { width + 0.4f, 0.3f, depth + 0.4f }, { 0.32f, 0.31f, 0.33f, 1 }, "", 1.0f, BodyType::Static, rot);
    // Rooftop clutter for the view from the hotel roof
    if (rnd() < 0.5f && rooftop) {
        vec3 p = c + f.t * ((rnd() - 0.5f) * (width - 3.0f)) - f.n * ((rnd() - 0.5f) * (depth - 3.0f)) + vec3(0, height + 0.3f, 0);
        if (rnd() < 0.5f) {
            Entity tank = Mesh(s, "Water Tank", "builtin:cylinder", p + vec3(0, 2.4f, 0), { 2.2f, 2.2f, 2.2f }, { 0.42f, 0.3f, 0.22f, 1 });
            Mat(tank).roughness = 0.9f;
            for (int i = 0; i < 4; i++)
                Decor(s, "Tank Leg", p + vec3(i % 2 ? 0.8f : -0.8f, 0.65f, i / 2 ? 0.8f : -0.8f), { 0.12f, 1.3f, 0.12f }, { 0.2f, 0.2f, 0.2f, 1 });
        } else {
            Decor(s, "AC Unit", p + vec3(0, 0.5f, 0), { 1.6f, 1.0f, 1.2f }, { 0.62f, 0.64f, 0.66f, 1 }, "", rot);
        }
    }
    if (!shop) return f;

    // Shop floor: door on the left, window across the rest
    float uDoor = -width * 0.5f + 1.6f;
    float winL = uDoor + 1.2f, winR = width * 0.5f - 0.8f;
    float uWin = (winL + winR) * 0.5f, winW = winR - winL;
    // The glass: a painted interior that glows (its own emissive map), tinted by the shop's light
    bool shut = shop->interior == "Textures/window_shutter.png";
    Entity glass = FrontBox(s, "Shop Window", f, uWin, 1.75f, { winW, 2.3f, 0.08f }, 0.04f, shut ? vec4(0.8f, 0.8f, 0.8f, 1) : vec4(0.25f, 0.25f, 0.25f, 1),
                            false, shop->interior);
    Mat(glass).roughness = shut ? 0.6f : 0.12f;
    if (!shut) {
        Mat(glass).emissiveMap = shop->interior;
        Glow(glass, shop->window * 0.55f);
    }
    FrontBox(s, "Window Sill", f, uWin, 0.55f, { winW + 0.2f, 0.12f, 0.18f }, 0.09f, { 0.6f, 0.58f, 0.55f, 1 });
    for (float u = winL + winW / 3; u < winR - 0.1f; u += winW / 3)
        FrontBox(s, "Mullion", f, u, 1.75f, { 0.08f, 2.3f, 0.1f }, 0.08f, { 0.12f, 0.12f, 0.13f, 1 });
    if (!shop->sign.empty()) Sign(s, f, uWin, 3.4f, std::min(3.4f, winW + 0.4f), shop->sign);
    else FrontBox(s, "Awning", f, uWin, 3.15f, { winW + 0.5f, 0.1f, 1.1f }, 0.55f, kAwnings[rnd.Int(4)]);
    Doorway(s, f, uDoor, shop->label, kDoorColors[rnd.Int(4)], shop->door, shop->spawn, shop->lamp || !shop->sign.empty());
    return f;
}

Shop Flavor(Rng& rnd, const std::string& label) {
    Shop sh;
    sh.label = label;
    sh.door = { { "Locked", true }, { "Locked Message", kLockedLines[rnd.Int(7)] } };
    sh.window = kWindowGlow[rnd.Int(4)];
    if (rnd() < 0.3f) sh.interior = "Textures/window_shutter.png";   // closed for the night
    return sh;
}

// ------------------------------------------------------------------ street furniture
void StreetLamp(Scene& s, const vec3& base, const vec3& toStreet) {
    Box(s, "Lamp Post", base + vec3(0, 2.7f, 0), { 0.16f, 5.4f, 0.16f }, { 0.12f, 0.13f, 0.14f, 1 });
    Decor(s, "Lamp Arm", base + vec3(0, 5.35f, 0) + toStreet * 0.6f, glm::abs(toStreet) * 1.2f + vec3(0.1f, 0.1f, 0.1f), { 0.12f, 0.13f, 0.14f, 1 });
    Glow(Decor(s, "Lamp Head", base + vec3(0, 5.25f, 0) + toStreet * 1.15f, { 0.5f, 0.16f, 0.5f }, { 1, 0.85f, 0.6f, 1 }), { 7.0f, 4.8f, 2.4f });
    PointLight(s, "Street Light", base + vec3(0, 4.9f, 0) + toStreet * 1.15f, { 1.0f, 0.74f, 0.45f }, 4.0f, 15.0f);
}

void Car(Scene& s, const std::string& name, const vec3& pos, float yaw, const vec4& paint) {
    Entity car = s.CreateEntity(name);
    car.Transform().position = pos;
    car.Transform().SetEulerDegrees({ 0, yaw, 0 });
    auto part = [&](Entity e) { e.Transform().parent = car.GetUUID(); return e; };
    Entity body = part(Box(s, "Body", { 0, 0.62f, 0 }, { 4.3f, 0.72f, 1.85f }, paint));
    Mat(body).metallic = 0.7f;
    Mat(body).roughness = 0.28f;
    Entity cabin = part(Mesh(s, "Cabin", "builtin:cube", { -0.25f, 1.27f, 0 }, { 2.3f, 0.6f, 1.66f }, { 0.07f, 0.08f, 0.1f, 1 }));
    Mat(cabin).roughness = 0.08f;
    Mat(cabin).metallic = 0.3f;
    for (int i = 0; i < 4; i++) {
        Entity w = part(Mesh(s, "Wheel", "builtin:cylinder", { i % 2 ? 1.35f : -1.35f, 0.34f, i / 2 ? 0.88f : -0.88f }, { 0.68f, 0.24f, 0.68f }, { 0.05f, 0.05f, 0.05f, 1 }));
        w.Transform().SetEulerDegrees({ 90, 0, 0 });
    }
    for (int k = -1; k <= 1; k += 2) {
        part(Glow(Mesh(s, "Headlight", "builtin:cube", { 2.16f, 0.72f, k * 0.62f }, { 0.04f, 0.14f, 0.36f }, { 1, 1, 0.9f, 1 }), { 1.2f, 1.2f, 1.0f }));
        part(Glow(Mesh(s, "Tail Light", "builtin:cube", { -2.16f, 0.75f, k * 0.66f }, { 0.04f, 0.12f, 0.3f }, { 0.8f, 0.1f, 0.1f, 1 }), { 2.0f, 0.15f, 0.1f }));
    }
}

} // namespace

// ------------------------------------------------------------------ City
void BuildCity(const std::string& path) {
    Scene s;
    s.name = "Divine City";
    auto& env = s.Environment();
    env.skyTop = { 0.06f, 0.07f, 0.18f };
    env.skyHorizon = { 0.72f, 0.38f, 0.38f };
    env.groundColor = { 0.12f, 0.1f, 0.12f };
    env.ambientIntensity = 0.42f;
    env.fogColor = { 0.30f, 0.20f, 0.28f };
    env.fogDensity = 0.013f;
    env.exposure = 1.1f;
    env.bloomIntensity = 0.12f;
    env.bloomThreshold = 0.9f;
    env.saturation = 1.12f;
    env.contrast = 1.08f;
    env.vignette = 0.3f;
    env.reflectionIntensity = 1.2f;
    // Low dusk sun from the west, straight down Main Street: long shadows, lit fronts.
    Entity sun = AddSun(s, { -11, -96, 0 }, 1.5f);
    sun.Get<LightComponent>().color = { 1.0f, 0.62f, 0.42f };

    Rng rnd{ 9 };

    // Ground: asphalt everywhere, sidewalk slabs under each block
    Entity road = Box(s, "Road", { 0, -0.5f, 0 }, { 170, 1, 170 }, { 0.9f, 0.9f, 0.95f, 1 }, "Textures/asphalt.png", 1.0f / 8.0f);
    Mat(road).roughness = 0.42f;   // just rained: the neon smears across it
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            Entity walk = Box(s, "Sidewalk", { sx * 45.0f, kWalk * 0.5f, sz * 45.0f }, { 80, kWalk, 80 }, vec4(1.0f), "Textures/sidewalk.png", 0.5f);
            Mat(walk).roughness = 0.75f;
            // Curb along both street edges
            Decor(s, "Curb", { sx * 45.0f, kWalk * 0.5f + 0.005f, sz * 5.1f }, { 80, kWalk + 0.01f, 0.2f }, { 0.62f, 0.6f, 0.58f, 1 });
            Decor(s, "Curb", { sx * 5.1f, kWalk * 0.5f + 0.005f, sz * 45.0f }, { 0.2f, kWalk + 0.01f, 80 }, { 0.62f, 0.6f, 0.58f, 1 });
        }

    // Road markings
    const vec4 paint(0.85f, 0.83f, 0.75f, 1);
    for (float d = 12.0f; d < 84.0f; d += 6.0f)
        for (int k = -1; k <= 1; k += 2) {
            Mesh(s, "Lane Dash", "builtin:cube", { k * d, 0.006f, 0 }, { 2.4f, 0.012f, 0.16f }, paint);
            Mesh(s, "Lane Dash", "builtin:cube", { 0, 0.006f, k * d }, { 0.16f, 0.012f, 2.4f }, paint);
        }
    for (int k = -1; k <= 1; k += 2)
        for (int i = -4; i <= 4; i++) {
            Mesh(s, "Crosswalk", "builtin:cube", { k * 7.0f, 0.006f, i * 1.05f }, { 2.6f, 0.012f, 0.55f }, paint);
            Mesh(s, "Crosswalk", "builtin:cube", { i * 1.05f, 0.006f, k * 7.0f }, { 0.55f, 0.012f, 2.6f }, paint);
        }

    // ---- Buildings. Special shops first.
    Shop noodle;
    noodle.label = "Noodle Bar";
    noodle.sign = "Textures/sign_noodles.png";
    noodle.door = { { "Target Scene", "Scenes/NoodleBar.scene" }, { "Spawn Point", "Spawn - Entrance" } };
    noodle.spawn = "Noodle Bar";
    noodle.window = { 1.0f, 0.55f, 0.35f };

    Shop apartment;
    apartment.label = "Apartment 9";
    apartment.sign = "Textures/sign_apartments.png";
    apartment.door = { { "Target Scene", "Scenes/Apartment.scene" }, { "Spawn Point", "Spawn - Entrance" }, { "Locked", true },
                       { "Locked Message", "It's locked. A brass plate on the door reads APT 9." }, { "Key Item", "Apartment Key" } };
    apartment.spawn = "Apartment 9";
    apartment.interior = "Textures/window_shutter.png";

    Shop hotel;
    hotel.label = "Hotel Stairs";
    hotel.sign = "Textures/sign_hotel.png";
    hotel.door = { { "Spawn Point", "Spawn - Hotel Roof" } };   // same scene: up to the roof
    hotel.spawn = "Hotel";
    hotel.window = { 0.6f, 0.8f, 1.0f };

    Shop subway;
    subway.label = "Subway - Ember Isle Line";
    subway.sign = "Textures/sign_subway.png";
    subway.door = { { "Target Scene", "Scenes/Island.scene" }, { "Hold", 1.6f } };
    subway.window = { 1.0f, 0.85f, 0.45f };

    Shop bar = Flavor(rnd, "Bar 9");
    bar.sign = "Textures/sign_bar9.png";
    bar.door["Locked Message"] = "Closed for a private party.\nThere's jazz playing behind the door.";
    bar.window = { 1.0f, 0.55f, 0.8f };
    bar.interior = "Textures/window_bar.png";
    Shop arcade = Flavor(rnd, "Arcade");
    arcade.sign = "Textures/sign_arcade.png";
    arcade.door["Locked Message"] = "The shutter's half down.\nSomeone inside is on the high score screen.";
    arcade.window = { 0.7f, 1.0f, 0.8f };
    arcade.interior = "Textures/window_arcade.png";

    // Corner buildings: 14 x 14 at each corner of the crossing, door on Main Street
    struct Corner { int sx, sz; float height; const Shop* shop; };
    Corner corners[] = { { 1, -1, 20.0f, &hotel }, { -1, 1, 16.0f, &bar }, { -1, -1, 28.0f, &arcade }, { 1, 1, 24.0f, nullptr } };
    Shop cornerFlavor = Flavor(rnd, "Pawn Shop");
    for (auto& k : corners) {
        Front f = MakeFront({ k.sx * 15.5f, 0, k.sz * kFront }, { 0, 0, (float)-k.sz });
        Building(s, rnd, k.shop == &hotel ? "Hotel" : "Corner Building", f, 14.0f, 14.0f, k.height, k.shop ? k.shop : &cornerFlavor, k.shop != &hotel);
    }

    // Rows along both streets in each quadrant
    const float mainWidths[4][5] = { { 10, 12, 9.5f, 8, 10 }, { 12, 9.5f, 10, 10, 8 }, { 9.5f, 8, 12, 10, 10 }, { 8, 10, 10, 9.5f, 12 } };
    const float crossWidths[4][5] = { { 12, 9, 11, 9.5f, 8 }, { 9, 11, 8, 12, 9.5f }, { 11, 9.5f, 12, 8, 9 }, { 9.5f, 12, 9, 11, 8 } };
    const char* names[] = { "Laundromat", "Tailor", "Records", "Pharmacy", "Bakery", "Video Rental", "Tobacco", "Barber", "Bookshop", "Florist" };
    int q = 0, shopIndex = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2, q++) {
            float a = 22.5f;
            for (int i = 0; i < 5; i++) {
                float w = mainWidths[q][i];
                Front f = MakeFront({ sx * (a + w * 0.5f), 0, sz * kFront }, { 0, 0, (float)-sz });
                const Shop* shop = nullptr;
                Shop flavor = Flavor(rnd, names[shopIndex++ % 10]);
                if (i == 0 && sx < 0 && sz < 0) shop = &apartment;
                else if (i == 0 && sx < 0 && sz > 0) shop = &subway;
                else shop = &flavor;
                float h = 12.0f + 4.0f * rnd.Int(5);
                Front built = Building(s, rnd, "Building", f, w, 13.0f, h, shop);
                if (shop == &subway) {
                    // Payphone beside the subway
                    vec3 p = built.At(w * 0.5f - 0.5f, 0, 0.9f);
                    Entity phone = Box(s, "Payphone", p + vec3(0, 1.3f, 0), { 0.5f, 0.7f, 0.35f }, { 0.15f, 0.3f, 0.55f, 1 }, "", 1.0f,
                                       BodyType::Static, { 0, built.yaw, 0 });
                    Decor(s, "Payphone Post", p + vec3(0, 0.5f, 0), { 0.12f, 1.0f, 0.12f }, { 0.2f, 0.2f, 0.22f, 1 });
                    AddExamine(phone, "Payphone", "No dial tone.\nSomeone has scratched NINE into the receiver.", 1.8f, { 0, -0.9f, 0 });
                }
                if (shop == &apartment) {
                    Entity poster = FrontBox(s, "Poster", built, w * 0.5f - 1.2f, 1.6f, { 0.8f, 1.1f, 0.03f }, 0.02f, { 0.95f, 0.92f, 0.78f, 1 });
                    AddExamine(poster, "Poster", "MISSING: GREY CAT. ANSWERS TO 'NINE'.\nThe reward has been crossed out.", 1.8f, { 0, -0.9f, 0.4f });
                }
                a += w;
            }
            a = 22.5f;
            for (int i = 0; i < 5; i++) {
                float w = crossWidths[q][i];
                Front f = MakeFront({ sx * kFront, 0, sz * (a + w * 0.5f) }, { (float)-sx, 0, 0 });
                Shop flavor = Flavor(rnd, names[shopIndex++ % 10]);
                const Shop* shop = (i == 0 && sx > 0 && sz > 0) ? &noodle : &flavor;
                float h = 12.0f + 4.0f * rnd.Int(5);
                Front built = Building(s, rnd, "Building", f, w, 13.0f, h, shop);
                if (shop == &noodle) {
                    // Vending machine next to the noodle bar
                    vec3 p = built.At(w * 0.5f - 0.9f, 0, 0.45f);
                    Entity vm = Box(s, "Vending Machine", p + vec3(0, kWalk + 0.95f, 0), { 1.0f, 1.9f, 0.8f }, { 0.75f, 0.1f, 0.12f, 1 }, "", 1.0f,
                                    BodyType::Static, { 0, built.yaw, 0 });
                    Glow(FrontBox(s, "Vending Panel", built, w * 0.5f - 0.9f, kWalk + 1.15f, { 0.75f, 1.1f, 0.04f }, 0.86f, { 0.9f, 0.95f, 1, 1 }),
                         { 1.4f, 1.6f, 1.8f });
                    AddExamine(vm, "Vending Machine", "Every button says SOLD OUT.", 1.8f, { 0, 0, 0.6f });
                }
                a += w;
            }
            // Block interior (only seen from rooftops)
            Box(s, "Back Lot", { sx * 47.0f, 4.5f, sz * 47.0f }, { 49, 9, 49 }, { 0.6f, 0.6f, 0.62f, 1 }, "Textures/facade_concrete.png", 1.0f / 16.0f);
        }

    // Street ends: a tall building across each end
    for (int k = -1; k <= 1; k += 2) {
        Building(s, rnd, "End Building", MakeFront({ k * kRowEnd, 0, 0 }, { (float)-k, 0, 0 }), 17.0f, 10.0f, 32.0f, nullptr);
        Building(s, rnd, "End Building", MakeFront({ 0, 0, k * kRowEnd }, { 0, 0, (float)-k }), 17.0f, 10.0f, 28.0f, nullptr);
    }

    // ---- Hotel roof (same-scene door target): parapet, stair hut with the way back down,
    // and a billboard you can see from the street.
    {
        const float roofY = 20.3f;
        vec3 c(15.5f, roofY, -15.5f);
        const vec4 wall(0.42f, 0.41f, 0.43f, 1);
        for (int k = -1; k <= 1; k += 2) {
            Box(s, "Parapet", c + vec3(k * 7.05f, 0.5f, 0), { 0.3f, 1.0f, 14.4f }, wall);
            Box(s, "Parapet", c + vec3(0, 0.5f, k * 7.05f), { 14.4f, 1.0f, 0.3f }, wall);
        }
        Box(s, "Stair Hut", { 19.5f, roofY + 1.5f, -19.5f }, { 3.0f, 3.0f, 3.0f }, { 0.55f, 0.52f, 0.5f, 1 });
        Decor(s, "Hut Roof", { 19.5f, roofY + 3.08f, -19.5f }, { 3.3f, 0.16f, 3.3f }, { 0.3f, 0.3f, 0.32f, 1 });
        Front hut = MakeFront({ 19.5f, roofY, -18.0f }, { 0, 0, 1 });
        Doorway(s, hut, 0.0f, "Stairs Down", kDoorColors[3], { { "Spawn Point", "Spawn - Hotel" } }, "Hotel Roof", true);
        PointLight(s, "Roof Light", hut.At(0, 2.6f, 0.6f), { 1.0f, 0.8f, 0.55f }, 2.0f, 8.0f);
        // Angled toward the crossing, so it reads from both Main Street and Cross Street
        const vec3 boardAt(13.2f, roofY, -11.2f), along(std::cos(glm::radians(-40.0f)), 0.0f, -std::sin(glm::radians(-40.0f)));
        for (int k = -1; k <= 1; k += 2) Box(s, "Billboard Post", boardAt + along * (k * 3.5f) + vec3(0, 1.6f, 0), { 0.25f, 3.2f, 0.25f }, { 0.15f, 0.15f, 0.16f, 1 });
        Entity board = Decor(s, "Billboard", boardAt + vec3(0, 4.3f, 0), { 9.0f, 2.25f, 0.3f }, vec4(1.0f), "Textures/sign_divine.png", { 0, -40.0f, 0 });
        Mat(board).emissiveMap = "Textures/sign_divine.png";
        Glow(board, vec3(2.6f));
        Entity view = s.CreateEntity("Roof Edge");
        view.Transform().position = c + vec3(-6.4f, 0.0f, 0.0f);
        AddExamine(view, "Look Out", "Divine City at dusk.\nFrom up here it almost looks peaceful.", 1.6f, vec3(0, 1.0f, 0));
        Decor(s, "AC Unit", c + vec3(-3.0f, 0.6f, -4.0f), { 2.0f, 1.2f, 1.4f }, { 0.65f, 0.66f, 0.68f, 1 });
    }

    // ---- Street furniture
    const float lampSpots[] = { 16, 32, 48, 64 };
    for (int i = 0; i < 4; i++)
        for (int k = -1; k <= 1; k += 2) {
            float side = (i % 2 == 0) == (k > 0) ? 1.0f : -1.0f;   // alternate sides
            StreetLamp(s, { k * lampSpots[i], kWalk, side * 5.6f }, { 0, 0, -side });
            StreetLamp(s, { side * 5.6f, kWalk, k * lampSpots[i] + 2.0f }, { -side, 0, 0 });
        }
    // Traffic lights on the four corners
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            vec3 p(sx * 6.0f, kWalk, sz * 6.0f);
            Box(s, "Signal Post", p + vec3(0, 2.1f, 0), { 0.16f, 4.2f, 0.16f }, { 0.12f, 0.13f, 0.14f, 1 });
            Decor(s, "Signal", p + vec3(0, 4.6f, 0), { 0.4f, 1.1f, 0.4f }, { 0.1f, 0.1f, 0.1f, 1 });
            bool green = sx == sz;
            Glow(Decor(s, green ? "Signal Green" : "Signal Red", p + vec3(0, green ? 4.3f : 4.9f, 0), { 0.42f, 0.24f, 0.42f },
                       green ? vec4(0.2f, 1, 0.5f, 1) : vec4(1, 0.15f, 0.1f, 1)),
                 green ? vec3(0.3f, 3.0f, 1.2f) : vec3(3.5f, 0.3f, 0.2f));
        }
    // Parked cars
    const vec4 paints[] = { { 0.55f, 0.06f, 0.06f, 1 }, { 0.85f, 0.85f, 0.82f, 1 }, { 0.1f, 0.2f, 0.4f, 1 }, { 0.12f, 0.12f, 0.13f, 1 },
                            { 0.8f, 0.6f, 0.12f, 1 }, { 0.2f, 0.4f, 0.3f, 1 } };
    Car(s, "Parked Car", { -40, 0, 3.6f }, 0, paints[0]);
    Car(s, "Parked Car", { -22, 0, -3.6f }, 180, paints[1]);
    Car(s, "Parked Car", { 26, 0, 3.6f }, 0, paints[2]);
    Car(s, "Parked Car", { 44, 0, -3.6f }, 180, paints[3]);
    Car(s, "Parked Car", { -3.6f, 0, -30 }, 90, paints[4]);
    Car(s, "Parked Car", { 3.6f, 0, 50 }, -90, paints[5]);
    AddExamine(s.FindByName("Parked Car"), "Parked Car", "The hood is still warm.\nWhoever parked it left in a hurry.", 2.8f, { 0, 0.5f, 0 });
    // Benches, bins and hydrants
    for (int k = -1; k <= 1; k += 2) {
        vec3 b(k * 38.0f, kWalk, k * 7.4f);
        Box(s, "Bench", b + vec3(0, 0.45f, 0), { 1.8f, 0.1f, 0.5f }, { 0.4f, 0.26f, 0.16f, 1 });
        Decor(s, "Bench Back", b + vec3(0, 0.8f, k * 0.22f), { 1.8f, 0.5f, 0.08f }, { 0.4f, 0.26f, 0.16f, 1 });
        Box(s, "Bench Legs", b + vec3(0, 0.2f, 0), { 1.5f, 0.4f, 0.3f }, { 0.1f, 0.1f, 0.11f, 1 });
        Entity bin = Mesh(s, "Trash Can", "builtin:cylinder", { k * 20.0f, kWalk + 0.5f, -k * 5.7f }, { 0.55f, 1.0f, 0.55f }, { 0.2f, 0.28f, 0.22f, 1 });
        bin.Add<BoxColliderComponent>();
        Entity hydrant = Mesh(s, "Hydrant", "builtin:cylinder", { -k * 5.7f, kWalk + 0.35f, k * 26.0f }, { 0.3f, 0.7f, 0.3f }, { 0.75f, 0.1f, 0.08f, 1 });
        hydrant.Add<BoxColliderComponent>();
    }
    // Neon accents on a few building corners
    const vec3 neon[] = { { 3.0f, 0.4f, 2.4f }, { 0.3f, 2.2f, 3.2f }, { 3.2f, 1.6f, 0.3f } };
    const vec3 neonSpots[] = { { 22.6f, 0, 8.6f }, { -22.6f, 0, -8.6f }, { -8.6f, 0, 22.6f }, { 8.6f, 0, -34.6f }, { 34.6f, 0, -8.6f } };
    int ni = 0;
    for (auto p : neonSpots) {
        Entity tube = Decor(s, "Neon Tube", p + vec3(0, 9.0f, 0), { 0.14f, 9.0f, 0.14f }, vec4(neon[ni % 3] * 0.3f, 1.0f));
        Glow(tube, neon[ni++ % 3]);
        if (ni == 2) AddScript(tube, "Flicker", { { "Amount", 0.5f }, { "Cut Outs", true } });
    }

    // ---- Player, camera, rules, sound
    PlayerRig rig = AddPlayer(s, { 6.8f, kWalk + 0.05f, 40.0f });
    AddGameManager(s, "DIVINE CITY", rig.player, {});
    AddControlsCard(s, "E / X - open doors, examine\n");
    AddAudio(s, "City Ambience", "Audio/city_ambience.wav", 0.55f, false, false);

    SceneSerializer(s).Save(path);
}

// ------------------------------------------------------------------ interiors
namespace {

struct Room { Scene& s; float w, d, h; };

// Floor, four walls and a ceiling around [-w/2,w/2] x [-d/2,d/2], open on the inside.
void BuildRoom(const Room& r, const std::string& floorTex, float floorTiling, const vec4& wallColor, const std::string& wallTex,
               const vec4& ceiling) {
    Scene& s = r.s;
    Entity floor = Box(s, "Floor", { 0, -0.1f, 0 }, { r.w + 0.6f, 0.2f, r.d + 0.6f }, vec4(1.0f), floorTex, floorTiling);
    Mat(floor).roughness = 0.4f;
    for (int k = -1; k <= 1; k += 2) {
        Box(s, "Wall", { 0, r.h * 0.5f, k * (r.d * 0.5f + 0.15f) }, { r.w + 0.6f, r.h, 0.3f }, wallColor, wallTex, 0.5f);
        Box(s, "Wall", { k * (r.w * 0.5f + 0.15f), r.h * 0.5f, 0 }, { 0.3f, r.h, r.d }, wallColor, wallTex, 0.5f);
        Decor(s, "Baseboard", { 0, 0.06f, k * (r.d * 0.5f - 0.01f) }, { r.w, 0.12f, 0.03f }, { 0.2f, 0.14f, 0.1f, 1 });
        Decor(s, "Baseboard", { k * (r.w * 0.5f - 0.01f), 0.06f, 0 }, { 0.03f, 0.12f, r.d }, { 0.2f, 0.14f, 0.1f, 1 });
    }
    Box(s, "Ceiling", { 0, r.h + 0.15f, 0 }, { r.w + 0.6f, 0.3f, r.d + 0.6f }, ceiling);
}

void InteriorEnvironment(Scene& s, const vec3& tint) {
    auto& env = s.Environment();
    env.skyTop = tint * 0.4f;
    env.skyHorizon = tint;
    env.groundColor = tint * 0.5f;
    env.ambientIntensity = 0.3f;
    env.fog = false;
    env.bloomIntensity = 0.1f;
    env.bloomThreshold = 0.9f;
    env.ssaoStrength = 1.0f;
    env.ssaoRadius = 0.45f;
    env.vignette = 0.32f;
    env.saturation = 1.1f;
}

void InteriorPlayer(Scene& s, const std::string& title, const vec3& cameraCorner) {
    Entity spawn = s.FindByName("Spawn - Entrance");
    PlayerRig rig = AddPlayer(s, spawn.Transform().position);
    rig.player.Transform().rotation = spawn.Transform().rotation;
    // Rooms get a fixed corner camera, like the old survival-horror games, instead of the
    // orbit camera (which would keep bumping into the walls).
    rig.camera.Transform().position = cameraCorner;
    for (auto& sc : rig.camera.Get<ScriptComponent>().scripts)
        if (sc.className == "ThirdPersonCamera") {
            sc.className = "FixedCamera";
            sc.fields = { { "Target", (uint64_t)rig.player.GetUUID() } };
        }
    AddGameManager(s, title, rig.player, {});
    AddControlsCard(s, "E / X - open doors, examine\n");
    AddAudio(s, "Room Tone", "Audio/room_tone.wav", 0.35f, false, false);
}

} // namespace

void BuildNoodleBar(const std::string& path) {
    Scene s;
    s.name = "Noodle Bar";
    InteriorEnvironment(s, { 0.22f, 0.13f, 0.09f });
    Room r{ s, 12.0f, 8.0f, 3.6f };
    BuildRoom(r, "Textures/tile_floor.png", 0.5f, { 0.95f, 0.9f, 0.85f, 1 }, "Textures/wallpaper.png", { 0.22f, 0.15f, 0.11f, 1 });
    // Wood wainscot along the walls (vertical planks via world-space UVs)
    for (int k = -1; k <= 1; k += 2) {
        Decor(s, "Wainscot", { 0, 0.55f, k * 3.97f }, { 12.0f, 1.1f, 0.06f }, { 0.75f, 0.6f, 0.5f, 1 }, "Textures/wood_floor.png");
        Decor(s, "Wainscot", { k * 5.97f, 0.55f, 0 }, { 0.06f, 1.1f, 8.0f }, { 0.75f, 0.6f, 0.5f, 1 }, "Textures/wood_floor.png");
    }
    // Counter with a lacquered top and a row of stools
    Box(s, "Counter", { 0.5f, 0.5f, -1.9f }, { 9.0f, 1.0f, 0.6f }, { 0.7f, 0.55f, 0.45f, 1 }, "Textures/wood_floor.png", 0.5f);
    Entity top = Box(s, "Counter Top", { 0.5f, 1.03f, -1.9f }, { 9.2f, 0.06f, 0.85f }, { 0.45f, 0.07f, 0.05f, 1 });
    Mat(top).roughness = 0.18f;
    for (int i = 0; i < 5; i++) {
        float x = -3.5f + i * 1.3f;
        Box(s, "Stool Post", { x, 0.35f, -1.15f }, { 0.08f, 0.7f, 0.08f }, { 0.7f, 0.7f, 0.72f, 1 });
        Entity seat = Mesh(s, "Stool", "builtin:cylinder", { x, 0.72f, -1.15f }, { 0.42f, 0.08f, 0.42f }, { 0.65f, 0.1f, 0.08f, 1 });
        Mat(seat).roughness = 0.35f;
    }
    // Kitchen
    Box(s, "Stove", { -3.0f, 0.45f, -3.5f }, { 2.4f, 0.9f, 0.7f }, { 0.55f, 0.56f, 0.58f, 1 });
    Entity pots = s.CreateEntity("Pots");
    pots.Transform().position = { -3.0f, 0.9f, -3.5f };
    for (int i = -1; i <= 1; i += 2) {
        Entity pot = Mesh(s, "Pot", "builtin:cylinder", { i * 0.55f, 0.25f, 0 }, { 0.6f, 0.5f, 0.6f }, { 0.65f, 0.66f, 0.68f, 1 }, pots);
        Mat(pot).metallic = 1.0f;
        Mat(pot).roughness = 0.35f;
        Entity steam = s.CreateEntity("Steam");
        steam.Transform().parent = pots.GetUUID();
        steam.Transform().position = { i * 0.55f, 0.6f, 0 };
        auto& pe = steam.Add<ParticleEmitterComponent>();
        ParticlePresets::Smoke(pe);
        pe.rate = 6;
        pe.startColor = { 0.9f, 0.9f, 0.9f, 0.25f };
        pe.shapeSize = vec3(0.2f);
    }
    AddExamine(pots, "Stock Pots", "Pork bone broth.\nIt smells like it's been simmering since 1987.", 3.0f);
    Entity fridge = Box(s, "Fridge", { 4.9f, 1.0f, -3.5f }, { 0.9f, 2.0f, 0.8f }, { 0.75f, 0.76f, 0.78f, 1 });
    Mat(fridge).metallic = 1.0f;
    Mat(fridge).roughness = 0.3f;
    for (float y : { 1.8f, 2.4f }) {
        Decor(s, "Shelf", { 0.5f, y, -3.8f }, { 4.0f, 0.06f, 0.4f }, { 0.3f, 0.2f, 0.14f, 1 });
        for (int i = 0; i < 6; i++)
            Mesh(s, "Bowl", "builtin:sphere", { -1.0f + i * 0.6f, y + 0.1f, -3.8f }, { 0.36f, 0.16f, 0.36f }, { 0.9f, 0.88f, 0.82f, 1 });
    }
    Entity menu = Decor(s, "Menu Board", { 0.5f, 3.0f, -3.95f }, { 2.6f, 0.82f, 0.06f }, vec4(1.0f), "Textures/sign_noodles.png");
    Mat(menu).emissiveMap = "Textures/sign_noodles.png";
    Glow(menu, vec3(1.8f));
    AddExamine(menu, "Menu", "TONIGHT: DIVINE RAMEN, 900 YEN.\nCash only. No exceptions.", 3.5f, { 0, -2.0f, 1.0f });
    // The key, under the tip jar at the end of the counter
    Entity jar = Mesh(s, "Tip Jar", "builtin:cylinder", { 4.25f, 1.18f, -1.8f }, { 0.18f, 0.25f, 0.18f }, { 0.7f, 0.85f, 0.9f, 1 });
    Mat(jar).roughness = 0.05f;
    Entity key = Mesh(s, "Apartment Key", "builtin:cube", { 3.95f, 1.075f, -1.75f }, { 0.16f, 0.025f, 0.07f }, { 0.95f, 0.75f, 0.3f, 1 });
    Mat(key).metallic = 1.0f;
    Mat(key).roughness = 0.25f;
    Mat(key).emissive = { 0.35f, 0.25f, 0.05f };
    AddExamine(key, "Key", "Under the tip jar: a brass key on a tag. APT 9.", 1.8f, vec3(0.0f),
               { { "Give Item", "Apartment Key" }, { "Hide When Taken", true } });
    // Tables by the window
    for (float x : { -1.6f, 2.6f }) {
        Box(s, "Table", { x, 0.75f, 2.5f }, { 1.3f, 0.06f, 0.8f }, { 0.25f, 0.16f, 0.1f, 1 });
        Decor(s, "Table Leg", { x, 0.37f, 2.5f }, { 0.1f, 0.74f, 0.1f }, { 0.1f, 0.1f, 0.1f, 1 });
        for (int k = -1; k <= 1; k += 2) Box(s, "Chair", { x + k * 0.95f, 0.45f, 2.5f }, { 0.45f, 0.08f, 0.45f }, { 0.55f, 0.1f, 0.08f, 1 });
    }
    // Front wall: window onto the street and the way out
    Entity street = Glow(Decor(s, "Window", { 0.6f, 1.75f, 3.98f }, { 4.0f, 1.6f, 0.04f }, { 0.03f, 0.03f, 0.05f, 1 }), { 0.02f, 0.018f, 0.05f });
    Mat(street).roughness = 1.0f;   // night glass: no sheen from the room's reflection probe
    Front exit = MakeFront({ -4.2f, 0, 4.0f }, { 0, 0, -1 });
    Doorway(s, exit, 0.0f, "Divine City", kDoorColors[0], { { "Target Scene", "Scenes/City.scene" }, { "Spawn Point", "Spawn - Noodle Bar" } },
            "Entrance", true);
    // Paper lanterns over the counter, a cold kitchen light, and the doorway lamp
    for (int i = -1; i <= 1; i++) {
        vec3 p(i * 3.0f + 0.5f, 2.85f, -1.6f);
        Glow(Mesh(s, "Lantern", "builtin:sphere", p, { 0.5f, 0.62f, 0.5f }, { 1, 0.35f, 0.2f, 1 }), { 1.6f, 0.32f, 0.1f });
        Decor(s, "Lantern Cord", p + vec3(0, 0.5f, 0), { 0.02f, 0.5f, 0.02f }, { 0.1f, 0.1f, 0.1f, 1 });
        PointLight(s, "Lantern Light", p - vec3(0, 0.2f, 0), { 1.0f, 0.5f, 0.32f }, 2.0f, 6.0f);
    }
    Entity strip = Glow(Decor(s, "Kitchen Strip Light", { -1.0f, 3.5f, -3.2f }, { 3.0f, 0.06f, 0.2f }, { 1, 1, 1, 1 }), { 3.0f, 3.2f, 3.4f });
    PointLight(s, "Kitchen Light", { -1.0f, 3.2f, -3.2f }, { 0.85f, 0.92f, 1.0f }, 1.6f, 6.0f);
    AddScript(strip, "Flicker", { { "Light", (uint64_t)s.FindByName("Kitchen Light").GetUUID() }, { "Amount", 0.15f }, { "Cut Outs", true } });
    PointLight(s, "Door Light", { -4.2f, 2.6f, 3.3f }, { 1.0f, 0.8f, 0.55f }, 1.2f, 5.0f);

    InteriorPlayer(s, "NOODLE BAR", { 5.5f, 3.25f, 3.6f });
    SceneSerializer(s).Save(path);
}

void BuildApartment(const std::string& path) {
    Scene s;
    s.name = "Apartment 9";
    InteriorEnvironment(s, { 0.1f, 0.12f, 0.18f });
    Room r{ s, 10.0f, 8.0f, 3.0f };
    BuildRoom(r, "Textures/wood_floor.png", 0.5f, { 0.5f, 0.55f, 0.5f, 1 }, "", { 0.7f, 0.7f, 0.68f, 1 });
    // Bed
    Box(s, "Bed", { -3.6f, 0.2f, -3.0f }, { 2.2f, 0.4f, 1.6f }, { 0.3f, 0.2f, 0.14f, 1 });
    Decor(s, "Mattress", { -3.6f, 0.5f, -3.0f }, { 2.1f, 0.22f, 1.5f }, { 0.85f, 0.85f, 0.82f, 1 });
    Decor(s, "Blanket", { -3.2f, 0.63f, -3.0f }, { 1.35f, 0.05f, 1.54f }, { 0.12f, 0.16f, 0.32f, 1 });
    Decor(s, "Pillow", { -4.4f, 0.67f, -3.0f }, { 0.45f, 0.14f, 1.1f }, { 0.95f, 0.95f, 0.92f, 1 });
    AddExamine(s.FindByName("Bed"), "Bed", "You're not tired.\nNot yet.", 2.2f);
    // Night stand + lamp
    Box(s, "Night Stand", { -2.0f, 0.28f, -3.6f }, { 0.5f, 0.56f, 0.45f }, { 0.3f, 0.2f, 0.14f, 1 });
    Glow(Mesh(s, "Lamp Shade", "builtin:cylinder", { -2.0f, 0.82f, -3.6f }, { 0.3f, 0.3f, 0.3f }, { 1, 0.85f, 0.6f, 1 }), { 1.8f, 1.3f, 0.7f });
    PointLight(s, "Bedside Light", { -2.0f, 1.1f, -3.4f }, { 1.0f, 0.75f, 0.5f }, 1.6f, 5.0f);
    // TV
    Box(s, "TV Cabinet", { 3.3f, 0.25f, -3.55f }, { 1.6f, 0.5f, 0.5f }, { 0.12f, 0.1f, 0.09f, 1 });
    Box(s, "TV", { 3.3f, 0.92f, -3.6f }, { 1.2f, 0.78f, 0.35f }, { 0.08f, 0.08f, 0.09f, 1 });
    Entity screen = Glow(Decor(s, "TV Screen", { 3.3f, 0.92f, -3.42f }, { 1.05f, 0.64f, 0.02f }, { 0.6f, 0.7f, 0.9f, 1 }), { 1.2f, 1.4f, 1.9f });
    Entity tvLight = PointLight(s, "TV Light", { 3.3f, 1.0f, -2.9f }, { 0.55f, 0.65f, 1.0f }, 1.4f, 5.5f);
    AddScript(screen, "Flicker", { { "Light", (uint64_t)tvLight.GetUUID() }, { "Amount", 0.45f }, { "Speed", 2.0f } });
    AddExamine(screen, "TV", "Static.\nFor a second you see a face you almost recognise.", 2.4f);
    // Desk with a note
    Box(s, "Desk", { 4.55f, 0.38f, 0.6f }, { 0.75f, 0.76f, 1.5f }, { 0.35f, 0.24f, 0.16f, 1 });
    Box(s, "Chair", { 3.8f, 0.45f, 0.6f }, { 0.45f, 0.08f, 0.45f }, { 0.2f, 0.2f, 0.22f, 1 });
    Entity note = Decor(s, "Note", { 4.5f, 0.77f, 0.5f }, { 0.22f, 0.006f, 0.3f }, { 0.95f, 0.93f, 0.85f, 1 });
    AddExamine(note, "Note", "In your own handwriting:\n\"The subway still runs to Ember Isle.\"", 1.9f);
    // Rug, plant, window, ceiling lamp
    Decor(s, "Rug", { 0.5f, 0.01f, 0.0f }, { 3.0f, 0.02f, 2.0f }, { 0.45f, 0.1f, 0.1f, 1 });
    Box(s, "Plant Pot", { -4.4f, 0.3f, 3.4f }, { 0.5f, 0.6f, 0.5f }, { 0.6f, 0.35f, 0.25f, 1 });
    Mesh(s, "Plant", "builtin:sphere", { -4.4f, 0.95f, 3.4f }, { 0.8f, 0.9f, 0.8f }, { 0.2f, 0.45f, 0.2f, 1 });
    Entity window = Glow(Decor(s, "Window", { 0.5f, 1.7f, -3.98f }, { 2.2f, 1.3f, 0.04f }, { 0.05f, 0.05f, 0.08f, 1 }), { 0.035f, 0.025f, 0.075f });
    Mat(window).roughness = 1.0f;
    AddExamine(window, "Window", "Neon, and rain that never quite falls.", 2.5f, { 0, -1.6f, 0.6f });
    Decor(s, "Window Frame", { 0.5f, 1.7f, -3.99f }, { 2.4f, 1.5f, 0.03f }, { 0.85f, 0.85f, 0.82f, 1 });
    Glow(Mesh(s, "Ceiling Lamp", "builtin:sphere", { 0.0f, 2.85f, 0.0f }, { 0.5f, 0.25f, 0.5f }, { 1, 0.95f, 0.85f, 1 }), { 2.5f, 2.3f, 2.0f });
    PointLight(s, "Ceiling Light", { 0.0f, 2.5f, 0.0f }, { 1.0f, 0.9f, 0.75f }, 1.8f, 8.0f);
    // Exit
    Front exit = MakeFront({ -3.0f, 0, 4.0f }, { 0, 0, -1 });
    Doorway(s, exit, 0.0f, "Divine City", kDoorColors[1], { { "Target Scene", "Scenes/City.scene" }, { "Spawn Point", "Spawn - Apartment 9" } },
            "Entrance", true);

    InteriorPlayer(s, "APARTMENT 9", { 4.5f, 2.7f, 3.5f });
    SceneSerializer(s).Save(path);
}

} // namespace demo
