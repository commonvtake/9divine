#include "euphoria/twod/Tilemap.h"
#include "euphoria/core/Xml.h"
#include "euphoria/core/Json.h"
#include "euphoria/core/FileSystem.h"
#include "euphoria/core/Log.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/renderer/Texture.h"
#include "euphoria/scene/Components.h"
#include <stb_image.h>
#include <sstream>
#include <set>

namespace eu {

// ======================================================================== Tileset / map
bool Tileset::Source(int id, std::string& outImage, ivec4& outRect) const {
    if (IsCollection()) {
        const TilesetTile* t = Tile(id);
        if (!t || t->image.empty()) return false;
        outImage = t->image;
        outRect = t->subRect.z > 0 ? t->subRect : ivec4(0, 0, t->imageWidth, t->imageHeight);
        return true;
    }
    if (columns <= 0 || id < 0 || (tileCount > 0 && id >= tileCount)) return false;
    int col = id % columns, row = id / columns;
    outImage = image;
    outRect = ivec4(margin + col * (tileWidth + spacing), margin + row * (tileHeight + spacing), tileWidth, tileHeight);
    return true;
}

int Tileset::Animated(int id, float time) const {
    const TilesetTile* t = Tile(id);
    if (!t || t->animation.empty() || t->animationLength <= 0.0f) return id;
    float local = std::fmod(time, t->animationLength);
    for (auto& f : t->animation) {
        if (local < f.duration) return f.tile;
        local -= f.duration;
    }
    return t->animation.back().tile;
}

const Tileset* TilemapData::FindTileset(uint32_t gid, int& localId) const {
    gid &= kTileGidMask;
    if (gid == 0) return nullptr;
    const Tileset* best = nullptr;
    for (auto& ts : tilesets)
        if (ts.firstGid <= gid && (!best || ts.firstGid > best->firstGid)) best = &ts;
    if (best) localId = (int)(gid - best->firstGid);
    return best;
}

const TilemapObject* TilemapData::FindObject(const std::string& name) const {
    for (auto& l : layers)
        for (auto& o : l.objects)
            if (o.name == name) return &o;
    return nullptr;
}

namespace {

std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
bool PropBool(const TiledProperties& p, const std::string& key, bool& out) {
    for (auto& [k, v] : p)
        if (Lower(k) == key) { out = v == "true" || v == "1"; return true; }
    return false;
}
bool PropInt(const TiledProperties& p, const std::string& key, int& out) {
    for (auto& [k, v] : p)
        if (Lower(k) == key) { out = std::atoi(v.c_str()); return true; }
    return false;
}

} // namespace

void TilemapData::AssignDrawOrder() {
    // Characters (sprites, order 0) walk among the objects of the "entity layer": by default the
    // last visible object layer that holds tile objects (trees, houses). Layers below it draw
    // under characters, layers above it over them (roofs, treetops). Tiled layer properties
    // override this: "ysort" (bool) marks the entity layer, "order" (int) sets a layer's order.
    int entity = -1;
    for (int i = 0; i < (int)layers.size(); i++) {
        bool ys = false;
        if (PropBool(layers[i].properties, "ysort", ys) && ys) entity = i;
    }
    if (entity < 0) {
        for (int i = 0; i < (int)layers.size(); i++) {
            auto& l = layers[i];
            if (l.type != TiledLayerType::Objects || !l.visible) continue;
            for (auto& o : l.objects) if (o.gid) { entity = i; break; }
        }
    }
    for (int i = 0; i < (int)layers.size(); i++) {
        auto& l = layers[i];
        l.ySort = i == entity;
        if (entity >= 0) l.order = i < entity ? -1000 + i : i == entity ? 0 : 1000 + i;
        else {
            std::string n = Lower(l.name);
            bool above = n.find("above") != std::string::npos || n.find("front") != std::string::npos || n.find("fore") != std::string::npos ||
                         n.find("overhead") != std::string::npos || n.find("roof") != std::string::npos;
            l.order = above ? 1000 + i : -1000 + i;
        }
        int o;
        if (PropInt(l.properties, "order", o)) l.order = o;
        bool ys;
        if (PropBool(l.properties, "ysort", ys)) l.ySort = ys;
    }
}

namespace {

// A tile's collision rectangles placed in map pixels, for a tile image whose top-left is at
// `topLeft` and drawn `scale` times its size, with Tiled's flip flags.
void PlaceTileColliders(const TilesetTile& t, int imgW, int imgH, uint32_t flags, const vec2& topLeft, const vec2& scale, std::vector<TileRect>& out) {
    for (TileRect r : t.colliders) {
        float w = (float)imgW, h = (float)imgH;
        if (flags & kTileFlipD) { std::swap(r.x, r.y); std::swap(r.w, r.h); std::swap(w, h); }
        if (flags & kTileFlipH) r.x = w - r.x - r.w;
        if (flags & kTileFlipV) r.y = h - r.y - r.h;
        out.push_back({ topLeft.x + r.x * scale.x, topLeft.y + r.y * scale.y, r.w * scale.x, r.h * scale.y });
    }
}

bool IsCollisionName(const std::string& name) {
    std::string n = Lower(name);
    static const char* names[] = { "collision", "collisions", "collider", "colliders", "collide", "solid", "solids", "walls", "wall", "blocking" };
    for (auto* x : names) if (n == x) return true;
    return false;
}

TileRect ObjectBounds(const TilemapObject& o) {
    if (o.shape == TiledShape::Polygon || o.shape == TiledShape::Polyline) {
        vec2 mn(1e30f), mx(-1e30f);
        for (auto& p : o.points) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
        if (o.points.empty()) return { o.x, o.y, 0, 0 };
        return { o.x + mn.x, o.y + mn.y, mx.x - mn.x, mx.y - mn.y };
    }
    return { o.x, o.y, o.width, o.height };
}

} // namespace

void TilemapData::BuildColliders() {
    std::vector<TileRect> rects;
    for (auto& l : layers) {
        bool layerSolid = l.collision;
        if (!l.visible && !layerSolid) continue;
        if (l.type == TiledLayerType::Tiles) {
            for (int cy = l.originY; cy < l.originY + l.height; cy++)
                for (int cx = l.originX; cx < l.originX + l.width; cx++) {
                    uint32_t g = l.At(cx, cy);
                    if (!(g & kTileGidMask)) continue;
                    vec2 cell((float)cx * tileWidth + l.offset.x, (float)cy * tileHeight + l.offset.y);
                    if (layerSolid) { rects.push_back({ cell.x, cell.y, (float)tileWidth, (float)tileHeight }); continue; }
                    int local = 0;
                    const Tileset* ts = FindTileset(g, local);
                    const TilesetTile* t = ts ? ts->Tile(local) : nullptr;
                    if (!t || t->colliders.empty()) continue;
                    std::string img;
                    ivec4 src;
                    if (!ts->Source(local, img, src)) continue;
                    // Tiles are drawn with their bottom-left on the cell's bottom-left.
                    vec2 topLeft(cell.x + ts->tileOffset.x, cell.y + tileHeight - src.w + ts->tileOffset.y);
                    PlaceTileColliders(*t, src.z, src.w, g, topLeft, vec2(1.0f), rects);
                }
        } else if (l.type == TiledLayerType::Objects) {
            for (auto& o : l.objects) {
                if (!o.visible) continue;
                bool objSolid = layerSolid || IsCollisionName(o.type);
                bool b;
                if (PropBool(o.properties, "collision", b)) objSolid = b;
                if (o.gid) {
                    int local = 0;
                    const Tileset* ts = FindTileset(o.gid, local);
                    const TilesetTile* t = ts ? ts->Tile(local) : nullptr;
                    std::string img;
                    ivec4 src;
                    if (!ts || !ts->Source(local, img, src) || src.z <= 0 || src.w <= 0) continue;
                    float w = o.width > 0 ? o.width : (float)src.z, h = o.height > 0 ? o.height : (float)src.w;
                    if (t && !t->colliders.empty())
                        PlaceTileColliders(*t, src.z, src.w, o.gid, vec2(o.x, o.y - h) + vec2(l.offset), vec2(w / src.z, h / src.w), rects);
                    else if (objSolid)
                        rects.push_back({ o.x + l.offset.x, o.y - h + l.offset.y, w, h });
                    continue;
                }
                if (!objSolid || o.shape == TiledShape::Point || o.shape == TiledShape::Text) continue;
                TileRect r = ObjectBounds(o);
                r.x += l.offset.x;
                r.y += l.offset.y;
                if (r.w > 0 && r.h > 0) rects.push_back(r);
            }
        }
    }
    colliders = Tiled::MergeRects(std::move(rects));
}

std::vector<TileRect> Tiled::MergeRects(std::vector<TileRect> rects) {
    const float eps = 0.01f;
    auto same = [&](float a, float b) { return std::abs(a - b) < eps; };
    rects.erase(std::remove_if(rects.begin(), rects.end(), [](const TileRect& r) { return r.w <= 0.0f || r.h <= 0.0f; }), rects.end());
    // Exact duplicates (stacked layers) first
    std::sort(rects.begin(), rects.end(), [](const TileRect& a, const TileRect& b) {
        return std::tie(a.y, a.h, a.x, a.w) < std::tie(b.y, b.h, b.x, b.w);
    });
    // Rows: same y and height, touching or overlapping horizontally
    std::vector<TileRect> rows;
    for (auto& r : rects) {
        if (!rows.empty()) {
            TileRect& p = rows.back();
            if (same(p.y, r.y) && same(p.h, r.h) && r.x <= p.x + p.w + eps) {
                p.w = std::max(p.x + p.w, r.x + r.w) - p.x;
                continue;
            }
        }
        rows.push_back(r);
    }
    // Columns: same x and width, touching vertically
    std::sort(rows.begin(), rows.end(), [](const TileRect& a, const TileRect& b) {
        return std::tie(a.x, a.w, a.y, a.h) < std::tie(b.x, b.w, b.y, b.h);
    });
    std::vector<TileRect> out;
    for (auto& r : rows) {
        if (!out.empty()) {
            TileRect& p = out.back();
            if (same(p.x, r.x) && same(p.w, r.w) && r.y <= p.y + p.h + eps) {
                p.h = std::max(p.y + p.h, r.y + r.h) - p.y;
                continue;
            }
        }
        out.push_back(r);
    }
    return out;
}

// ======================================================================== Loading
namespace {

std::string MakePath(const fs::path& baseDir, const std::string& source) {
    if (source.empty()) return "";
    fs::path p = (baseDir / fs::u8path(source)).lexically_normal();
    if (Project::IsLoaded()) return Project::MakeRelative(p);
    return p.generic_string();
}

vec4 ParseColor(std::string s) {
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    auto hex = [&](size_t i) { return i + 2 <= s.size() ? (float)std::strtoul(s.substr(i, 2).c_str(), nullptr, 16) / 255.0f : 1.0f; };
    if (s.size() == 8) return { hex(2), hex(4), hex(6), hex(0) };   // #AARRGGBB
    if (s.size() == 6) return { hex(0), hex(2), hex(4), 1.0f };
    return vec4(1.0f);
}

void ReadProperties(const XmlNode& n, TiledProperties& out) {
    const XmlNode* ps = n.Child("properties");
    if (!ps) return;
    for (auto* p : ps->ChildrenNamed("property")) {
        std::string v = p->Attr("value") ? p->Str("value") : p->text;
        out[p->Str("name")] = v;
    }
}

TilemapObject ReadObject(const XmlNode& n) {
    TilemapObject o;
    o.id = n.Int("id");
    o.name = n.Str("name");
    o.type = n.Attr("class") ? n.Str("class") : n.Str("type");
    o.x = n.Float("x");
    o.y = n.Float("y");
    o.width = n.Float("width");
    o.height = n.Float("height");
    o.rotation = n.Float("rotation");
    o.gid = (uint32_t)std::strtoul(n.Str("gid", "0").c_str(), nullptr, 10);
    o.visible = n.Bool("visible", true);
    if (o.gid) o.shape = TiledShape::Tile;
    if (n.Child("ellipse")) o.shape = TiledShape::Ellipse;
    if (n.Child("point")) o.shape = TiledShape::Point;
    if (n.Child("text")) o.shape = TiledShape::Text;
    const XmlNode* poly = n.Child("polygon");
    if (!poly) { poly = n.Child("polyline"); if (poly) o.shape = TiledShape::Polyline; }
    else o.shape = TiledShape::Polygon;
    if (poly) {
        std::istringstream ss(poly->Str("points"));
        std::string pair;
        while (ss >> pair) {
            size_t c = pair.find(',');
            if (c == std::string::npos) continue;
            o.points.push_back({ std::strtof(pair.c_str(), nullptr), std::strtof(pair.c_str() + c + 1, nullptr) });
        }
    }
    ReadProperties(n, o.properties);
    return o;
}

bool ReadTilesetNode(const XmlNode& n, const fs::path& baseDir, Tileset& ts) {
    ts.name = n.Str("name");
    ts.tileWidth = n.Int("tilewidth", 16);
    ts.tileHeight = n.Int("tileheight", 16);
    ts.spacing = n.Int("spacing");
    ts.margin = n.Int("margin");
    ts.tileCount = n.Int("tilecount");
    ts.columns = n.Int("columns");
    if (auto* off = n.Child("tileoffset")) ts.tileOffset = ivec2(off->Int("x"), off->Int("y"));
    if (auto* img = n.Child("image")) {
        ts.image = MakePath(baseDir, img->Str("source"));
        ts.imageWidth = img->Int("width");
        ts.imageHeight = img->Int("height");
        if (ts.columns <= 0 && ts.imageWidth > 0)
            ts.columns = std::max(1, (ts.imageWidth - 2 * ts.margin + ts.spacing) / (ts.tileWidth + ts.spacing));
    }
    ReadProperties(n, ts.properties);
    for (auto* tn : n.ChildrenNamed("tile")) {
        int id = tn->Int("id");
        TilesetTile& t = ts.tiles[id];
        t.type = tn->Attr("class") ? tn->Str("class") : tn->Str("type");
        if (auto* img = tn->Child("image")) {
            t.image = MakePath(baseDir, img->Str("source"));
            t.imageWidth = img->Int("width");
            t.imageHeight = img->Int("height");
            if (tn->Attr("width")) t.subRect = ivec4(tn->Int("x"), tn->Int("y"), tn->Int("width"), tn->Int("height"));
        }
        if (auto* anim = tn->Child("animation")) {
            for (auto* f : anim->ChildrenNamed("frame")) {
                float d = std::max(1, f->Int("duration", 100)) / 1000.0f;
                t.animation.push_back({ f->Int("tileid"), d });
                t.animationLength += d;
            }
        }
        if (auto* og = tn->Child("objectgroup")) {
            for (auto* on : og->ChildrenNamed("object")) {
                TilemapObject o = ReadObject(*on);
                if (o.shape == TiledShape::Point || o.shape == TiledShape::Text) continue;
                TileRect r = ObjectBounds(o);
                if (r.w > 0.0f && r.h > 0.0f) t.colliders.push_back(r);
            }
        }
        ReadProperties(*tn, t.properties);
    }
    // Image collections whose images lack a size in the file: read it from the image.
    for (auto& [id, t] : ts.tiles) {
        if (t.image.empty() || (t.imageWidth > 0 && t.imageHeight > 0)) continue;
        int w = 0, h = 0, c = 0;
        if (stbi_info(Project::Resolve(t.image).string().c_str(), &w, &h, &c)) { t.imageWidth = w; t.imageHeight = h; }
    }
    if (!ts.image.empty() && (ts.imageWidth <= 0 || ts.imageHeight <= 0)) {
        int w = 0, h = 0, c = 0;
        if (stbi_info(Project::Resolve(ts.image).string().c_str(), &w, &h, &c)) {
            ts.imageWidth = w; ts.imageHeight = h;
            if (ts.columns <= 0) ts.columns = std::max(1, (w - 2 * ts.margin + ts.spacing) / (ts.tileWidth + ts.spacing));
        }
    }
    return true;
}

// Tiled's JSON formats are the same model as the XML ones; they are converted to the XML
// shape so one reader handles both.
std::string JStr(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number()) { std::ostringstream o; o << v.get<double>(); return o.str(); }
    return v.dump();
}
void CopyAttrs(const json& j, XmlNode& n, std::initializer_list<const char*> keys) {
    for (auto* k : keys) {
        auto it = j.find(k);
        if (it == j.end() || it->is_null() || it->is_object() || it->is_array()) continue;
        std::string v = JStr(*it);
        if (it->is_boolean()) v = it->get<bool>() ? "1" : "0";
        n.attributes.emplace_back(k, v);
    }
}
void JsonProps(const json& j, XmlNode& n) {
    if (!j.contains("properties") || !j["properties"].is_array()) return;
    XmlNode ps;
    ps.name = "properties";
    for (auto& p : j["properties"]) {
        XmlNode pn;
        pn.name = "property";
        pn.attributes = { { "name", JStr(p.value("name", json(""))) }, { "type", JStr(p.value("type", json("string"))) },
                          { "value", p.contains("value") ? JStr(p["value"]) : "" } };
        ps.children.push_back(pn);
    }
    n.children.push_back(ps);
}
XmlNode JsonObject(const json& j) {
    XmlNode n;
    n.name = "object";
    CopyAttrs(j, n, { "id", "name", "type", "class", "x", "y", "width", "height", "rotation", "gid", "visible", "template" });
    if (j.value("ellipse", false)) n.children.push_back(XmlNode{ "ellipse" });
    if (j.value("point", false)) n.children.push_back(XmlNode{ "point" });
    if (j.contains("text")) n.children.push_back(XmlNode{ "text" });
    for (const char* key : { "polygon", "polyline" }) {
        if (!j.contains(key)) continue;
        std::string pts;
        for (auto& p : j[key]) pts += JStr(p.value("x", json(0))) + "," + JStr(p.value("y", json(0))) + " ";
        XmlNode pn;
        pn.name = key;
        pn.attributes = { { "points", pts } };
        n.children.push_back(pn);
    }
    JsonProps(j, n);
    return n;
}
XmlNode JsonTileset(const json& j) {
    XmlNode n;
    n.name = "tileset";
    CopyAttrs(j, n, { "firstgid", "source", "name", "tilewidth", "tileheight", "spacing", "margin", "tilecount", "columns" });
    if (j.contains("image")) {
        XmlNode img;
        img.name = "image";
        img.attributes = { { "source", JStr(j["image"]) }, { "width", JStr(j.value("imagewidth", json(0))) }, { "height", JStr(j.value("imageheight", json(0))) } };
        n.children.push_back(img);
    }
    if (j.contains("tileoffset")) {
        XmlNode off;
        off.name = "tileoffset";
        CopyAttrs(j["tileoffset"], off, { "x", "y" });
        n.children.push_back(off);
    }
    for (auto& jt : j.value("tiles", json::array())) {
        XmlNode t;
        t.name = "tile";
        CopyAttrs(jt, t, { "id", "type", "class", "x", "y", "width", "height" });
        if (jt.contains("image")) {
            XmlNode img;
            img.name = "image";
            img.attributes = { { "source", JStr(jt["image"]) }, { "width", JStr(jt.value("imagewidth", json(0))) }, { "height", JStr(jt.value("imageheight", json(0))) } };
            t.children.push_back(img);
        }
        if (jt.contains("animation")) {
            XmlNode a;
            a.name = "animation";
            for (auto& f : jt["animation"]) {
                XmlNode fn;
                fn.name = "frame";
                CopyAttrs(f, fn, { "tileid", "duration" });
                a.children.push_back(fn);
            }
            t.children.push_back(a);
        }
        if (jt.contains("objectgroup")) {
            XmlNode og;
            og.name = "objectgroup";
            for (auto& o : jt["objectgroup"].value("objects", json::array())) og.children.push_back(JsonObject(o));
            t.children.push_back(og);
        }
        JsonProps(jt, t);
        n.children.push_back(t);
    }
    JsonProps(j, n);
    return n;
}
XmlNode JsonData(const json& d, const json& layer, const char* name) {
    XmlNode n;
    n.name = name;
    if (d.is_array()) {
        n.attributes = { { "encoding", "csv" } };
        std::string s;
        for (auto& v : d) { s += JStr(v); s += ','; }
        n.text = s;
    } else if (d.is_string()) {
        n.attributes = { { "encoding", layer.value("encoding", std::string("base64")) } };
        std::string comp = layer.value("compression", std::string());
        if (!comp.empty()) n.attributes.emplace_back("compression", comp);
        n.text = d.get<std::string>();
    }
    return n;
}
XmlNode JsonLayer(const json& j) {
    XmlNode n;
    std::string type = j.value("type", std::string("tilelayer"));
    n.name = type == "objectgroup" ? "objectgroup" : type == "imagelayer" ? "imagelayer" : type == "group" ? "group" : "layer";
    CopyAttrs(j, n, { "id", "name", "class", "width", "height", "visible", "opacity", "tintcolor", "offsetx", "offsety", "parallaxx", "parallaxy", "repeatx", "repeaty" });
    if (type == "tilelayer") {
        if (j.contains("chunks")) {
            XmlNode data;
            data.name = "data";
            data.attributes = { { "encoding", "csv" } };
            for (auto& c : j["chunks"]) {
                XmlNode cn = JsonData(c["data"], j, "chunk");
                CopyAttrs(c, cn, { "x", "y", "width", "height" });
                data.attributes = { { "encoding", cn.Str("encoding") } };
                if (cn.Attr("compression")) data.attributes.emplace_back("compression", cn.Str("compression"));
                data.children.push_back(cn);
            }
            n.children.push_back(data);
        } else if (j.contains("data")) {
            n.children.push_back(JsonData(j["data"], j, "data"));
        }
    } else if (type == "objectgroup") {
        for (auto& o : j.value("objects", json::array())) n.children.push_back(JsonObject(o));
    } else if (type == "imagelayer") {
        XmlNode img;
        img.name = "image";
        img.attributes = { { "source", j.value("image", std::string()) }, { "width", JStr(j.value("imagewidth", json(0))) }, { "height", JStr(j.value("imageheight", json(0))) } };
        n.children.push_back(img);
    } else if (type == "group") {
        for (auto& l : j.value("layers", json::array())) n.children.push_back(JsonLayer(l));
    }
    JsonProps(j, n);
    return n;
}
XmlNode JsonMap(const json& j) {
    XmlNode n;
    n.name = "map";
    CopyAttrs(j, n, { "width", "height", "tilewidth", "tileheight", "infinite", "orientation", "backgroundcolor" });
    for (auto& t : j.value("tilesets", json::array())) n.children.push_back(JsonTileset(t));
    for (auto& l : j.value("layers", json::array())) n.children.push_back(JsonLayer(l));
    JsonProps(j, n);
    return n;
}

bool DecodeGids(const XmlNode& data, const std::string& encoding, const std::string& compression, std::vector<uint32_t>& out, std::string& warn) {
    out.clear();
    if (encoding == "csv") {
        const char* p = data.text.c_str();
        while (*p) {
            while (*p && !std::isdigit((unsigned char)*p)) p++;
            if (!*p) break;
            char* end = nullptr;
            out.push_back((uint32_t)std::strtoul(p, &end, 10));
            p = end;
        }
        return true;
    }
    if (encoding == "base64") {
        std::string b64;
        for (char c : data.text) if (!std::isspace((unsigned char)c)) b64 += c;
        std::vector<uint8_t> bytes = Base64Decode(b64);
        if (compression == "zlib" || compression == "gzip") {
            const char* src = (const char*)bytes.data();
            int len = (int)bytes.size(), outLen = 0;
            char* raw = nullptr;
            if (compression == "zlib") {
                raw = stbi_zlib_decode_malloc_guesssize_headerflag(src, len, len * 4 + 64, &outLen, 1);
            } else if (len > 18 && (uint8_t)src[0] == 0x1f && (uint8_t)src[1] == 0x8b) {
                // gzip: skip the header, then raw deflate
                int flags = (uint8_t)src[3], pos = 10;
                if (flags & 4) pos += 2 + ((uint8_t)src[pos] | ((uint8_t)src[pos + 1] << 8));
                if (flags & 8) { while (pos < len && src[pos]) pos++; pos++; }
                if (flags & 16) { while (pos < len && src[pos]) pos++; pos++; }
                if (flags & 2) pos += 2;
                if (pos < len) raw = stbi_zlib_decode_noheader_malloc(src + pos, len - pos, &outLen);
            }
            if (!raw) { warn = "could not decompress " + compression + " tile data"; return false; }
            bytes.assign(raw, raw + outLen);
            stbi_image_free(raw);   // stb uses free() for both
        } else if (!compression.empty()) {
            warn = compression + " compressed tile data isn't supported - in Tiled, set the map's Tile Layer Format to CSV or Base64 (zlib)";
            return false;
        }
        out.resize(bytes.size() / 4);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = (uint32_t)bytes[i * 4] | ((uint32_t)bytes[i * 4 + 1] << 8) | ((uint32_t)bytes[i * 4 + 2] << 16) | ((uint32_t)bytes[i * 4 + 3] << 24);
        return true;
    }
    // Legacy XML: <tile gid="..."/>
    for (auto& c : data.children)
        if (c.name == "tile") out.push_back((uint32_t)std::strtoul(c.Str("gid", "0").c_str(), nullptr, 10));
    return true;
}

struct Inherit {
    bool visible = true;
    float opacity = 1.0f;
    vec4 tint{ 1.0f };
    vec2 offset{ 0.0f };
    vec2 parallax{ 1.0f };
};

void ReadLayers(const XmlNode& parent, const Inherit& in, const fs::path& baseDir, TilemapData& map) {
    for (auto& n : parent.children) {
        if (n.name != "layer" && n.name != "objectgroup" && n.name != "imagelayer" && n.name != "group") continue;
        Inherit me;
        me.visible = in.visible && n.Bool("visible", true);
        me.opacity = in.opacity * n.Float("opacity", 1.0f);
        me.tint = in.tint * (n.Attr("tintcolor") ? ParseColor(n.Str("tintcolor")) : vec4(1.0f));
        me.offset = in.offset + vec2(n.Float("offsetx"), n.Float("offsety"));
        me.parallax = in.parallax * vec2(n.Float("parallaxx", 1.0f), n.Float("parallaxy", 1.0f));
        if (n.name == "group") { ReadLayers(n, me, baseDir, map); continue; }

        TilemapLayer l;
        l.name = n.Str("name");
        l.className = n.Str("class");
        l.visible = me.visible;
        l.opacity = me.opacity;
        l.tint = me.tint;
        l.offset = me.offset;
        l.parallax = me.parallax;
        ReadProperties(n, l.properties);
        bool solid = false;
        l.collision = (PropBool(l.properties, "collision", solid) || PropBool(l.properties, "collides", solid) || PropBool(l.properties, "solid", solid))
                          ? solid : IsCollisionName(l.name) || IsCollisionName(l.className);

        if (n.name == "layer") {
            l.type = TiledLayerType::Tiles;
            const XmlNode* data = n.Child("data");
            if (!data) continue;
            std::string enc = data->Str("encoding"), comp = data->Str("compression");
            auto chunks = data->ChildrenNamed("chunk");
            std::string warn;
            if (chunks.empty()) {
                l.width = n.Int("width", map.width);
                l.height = n.Int("height", map.height);
                if (!DecodeGids(*data, enc, comp, l.gids, warn)) { map.warnings.push_back("Layer '" + l.name + "': " + warn); continue; }
                l.gids.resize((size_t)l.width * l.height, 0);
            } else {
                int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
                for (auto* c : chunks) {
                    x0 = std::min(x0, c->Int("x")); y0 = std::min(y0, c->Int("y"));
                    x1 = std::max(x1, c->Int("x") + c->Int("width")); y1 = std::max(y1, c->Int("y") + c->Int("height"));
                }
                l.originX = x0; l.originY = y0; l.width = x1 - x0; l.height = y1 - y0;
                l.gids.assign((size_t)l.width * l.height, 0);
                for (auto* c : chunks) {
                    std::vector<uint32_t> g;
                    if (!DecodeGids(*c, enc, comp, g, warn)) { map.warnings.push_back("Layer '" + l.name + "': " + warn); break; }
                    int cw = c->Int("width"), ch = c->Int("height"), cx = c->Int("x") - x0, cy = c->Int("y") - y0;
                    for (int y = 0; y < ch; y++)
                        for (int x = 0; x < cw && (size_t)(y * cw + x) < g.size(); x++)
                            l.gids[(size_t)(cy + y) * l.width + cx + x] = g[(size_t)y * cw + x];
                }
            }
        } else if (n.name == "objectgroup") {
            l.type = TiledLayerType::Objects;
            for (auto* o : n.ChildrenNamed("object")) {
                if (o->Attr("template")) map.warnings.push_back("Object templates (.tx) aren't supported: object " + o->Str("id") + " in '" + l.name + "' skipped");
                else l.objects.push_back(ReadObject(*o));
            }
        } else {
            l.type = TiledLayerType::Image;
            l.repeatX = n.Bool("repeatx");
            l.repeatY = n.Bool("repeaty");
            if (auto* img = n.Child("image")) {
                l.image = MakePath(baseDir, img->Str("source"));
                l.imageWidth = img->Int("width");
                l.imageHeight = img->Int("height");
            }
        }
        map.layers.push_back(std::move(l));
    }
}

bool ReadExternalTileset(const fs::path& file, Tileset& ts, std::string& err) {
    auto text = FileSystem::ReadText(file);
    if (!text) { err = "tileset not found: " + file.generic_string(); return false; }
    std::string ext = Lower(file.extension().string());
    XmlNode root;
    size_t first = text->find_first_not_of(" \t\r\n");
    if (ext == ".tsj" || ext == ".json" || (first != std::string::npos && (*text)[first] == '{')) {
        try { root = JsonTileset(json::parse(*text)); }
        catch (const std::exception& e) { err = file.filename().string() + ": " + e.what(); return false; }
    } else if (!ParseXml(*text, root, &err)) {
        err = file.filename().string() + ": " + err;
        return false;
    }
    return ReadTilesetNode(root, file.parent_path(), ts);
}

} // namespace

Ref<TilemapData> Tiled::LoadFromText(const std::string& text, bool isJson, const std::string& baseDirStr, std::string* error) {
    fs::path baseDir = fs::u8path(baseDirStr);
    XmlNode root;
    std::string err;
    if (isJson) {
        try { root = JsonMap(json::parse(text)); }
        catch (const std::exception& e) { if (error) *error = e.what(); return nullptr; }
    } else if (!ParseXml(text, root, &err)) {
        if (error) *error = err;
        return nullptr;
    }
    if (root.name != "map") { if (error) *error = "not a Tiled map (root element is <" + root.name + ">)"; return nullptr; }

    auto map = MakeRef<TilemapData>();
    map->width = root.Int("width");
    map->height = root.Int("height");
    map->tileWidth = std::max(1, root.Int("tilewidth", 16));
    map->tileHeight = std::max(1, root.Int("tileheight", 16));
    map->infinite = root.Bool("infinite");
    map->orientation = root.Str("orientation", "orthogonal");
    if (root.Attr("backgroundcolor")) { map->hasBackground = true; map->background = ParseColor(root.Str("backgroundcolor")); }
    if (map->orientation != "orthogonal")
        map->warnings.push_back("'" + map->orientation + "' maps are drawn as orthogonal (only orthogonal maps are supported)");
    ReadProperties(root, map->properties);

    for (auto* tn : root.ChildrenNamed("tileset")) {
        Tileset ts;
        ts.firstGid = tn->UInt("firstgid", 1);
        if (tn->Attr("source")) {
            std::string terr;
            fs::path tsPath = (baseDir / fs::u8path(tn->Str("source"))).lexically_normal();
            map->dependencies.push_back(tsPath.u8string());
            if (!ReadExternalTileset(tsPath, ts, terr)) {
                map->warnings.push_back(terr);
                continue;
            }
            ts.firstGid = tn->UInt("firstgid", 1);
        } else {
            ReadTilesetNode(*tn, baseDir, ts);
        }
        map->tilesets.push_back(std::move(ts));
    }
    std::sort(map->tilesets.begin(), map->tilesets.end(), [](const Tileset& a, const Tileset& b) { return a.firstGid < b.firstGid; });
    ReadLayers(root, Inherit{}, baseDir, *map);

    // Used area
    if (!map->infinite) map->bounds = ivec4(0, 0, map->width, map->height);
    else {
        int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
        for (auto& l : map->layers) {
            if (l.type != TiledLayerType::Tiles || l.width == 0) continue;
            x0 = std::min(x0, l.originX); y0 = std::min(y0, l.originY);
            x1 = std::max(x1, l.originX + l.width); y1 = std::max(y1, l.originY + l.height);
        }
        map->bounds = x0 <= x1 ? ivec4(x0, y0, x1 - x0, y1 - y0) : ivec4(0);
        map->width = map->bounds.z;
        map->height = map->bounds.w;
    }
    map->AssignDrawOrder();
    map->BuildColliders();
    return map;
}

Ref<TilemapData> Tiled::Load(const std::string& absolutePath, std::string* error) {
    fs::path p = fs::u8path(absolutePath);
    auto text = FileSystem::ReadText(p);
    if (!text) { if (error) *error = "file not found"; return nullptr; }
    std::string ext = Lower(p.extension().string());
    size_t first = text->find_first_not_of(" \t\r\n\xEF\xBB\xBF");
    bool isJson = ext == ".tmj" || ext == ".json" || (first != std::string::npos && (*text)[first] == '{');
    auto map = LoadFromText(*text, isJson, p.parent_path().u8string(), error);
    if (map) map->path = Project::IsLoaded() ? Project::MakeRelative(p) : p.generic_string();
    return map;
}

bool Tiled::IsMapFile(const std::string& path) {
    std::string ext = Lower(fs::u8path(path).extension().string());
    return ext == ".tmx" || ext == ".tmj";
}

TilemapData* EnsureTilemapLoaded(TilemapComponent& c) {
    if (c.map.empty()) { c.runtime.reset(); c.loadedMap.clear(); return nullptr; }
    if (!c.runtime || c.loadedMap != c.map || c.loadedVersion != AssetManager::Version()) {
        c.runtime = MakeRef<TilemapRuntime>();
        c.runtime->data = AssetManager::GetTilemap(c.map);
        c.loadedMap = c.map;
        c.loadedVersion = AssetManager::Version();
    }
    return c.runtime->data.get();
}

Ref<Texture2D> TilemapRuntime::Texture(const std::string& image) {
    auto it = textures.find(image);
    if (it != textures.end()) return it->second;
    auto t = AssetManager::GetTexture(image, true);
    textures[image] = t;
    return t;
}

// ======================================================================== Sprite sheets
namespace SpriteSheet {
namespace {
// Largest frame count n (dividing len) where every slot has content and no content crosses a
// slot boundary. 0 when only n = 1 works.
int GapCount(const std::vector<float>& occ, bool allowEmpty) {
    int len = (int)occ.size(), best = 0;
    for (int n = 2; n <= len; n++) {
        if (len % n) continue;
        int s = len / n;
        if (s < 4) break;
        bool ok = true;
        for (int k = 0; k < n && ok; k++) {
            if (k > 0 && occ[k * s - 1] > 0 && occ[k * s] > 0) ok = false;
            if (!allowEmpty) {
                bool any = false;
                for (int i = k * s; i < (k + 1) * s && !any; i++) any = occ[i] > 0;
                if (!any) ok = false;
            }
        }
        if (ok) best = n;
    }
    return best;
}
// Touching frames: the slot size whose boundaries fall where the silhouettes are thinnest.
int ThinBoundarySize(const std::vector<float>& sum) {
    int len = (int)sum.size();
    double mean = 0;
    for (float v : sum) mean += v;
    mean = mean / std::max(1, len) + 1e-6;
    std::vector<std::pair<int, double>> scores;
    for (int p = 8; p < len; p++) {
        if (len % p) continue;
        double s = 0;
        int n = 0;
        for (int b = p; b < len; b += p) { s += (sum[b - 1] + sum[b]) * 0.5; n++; }
        scores.push_back({ p, s / n / mean });
    }
    if (scores.empty()) return 0;
    double best = 1e9;
    for (auto& [p, s] : scores) best = std::min(best, s);
    if (best > 0.6) return 0;
    for (auto& [p, s] : scores)
        if (s <= best + 0.08) return p;
    return 0;
}
}

ivec2 DetectFrameSize(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return { w, h };
    std::vector<float> colOcc(w, 0.0f), rowOcc(h, 0.0f), colSum(w, 0.0f), rowSum(h, 0.0f);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float a = rgba[((size_t)y * w + x) * 4 + 3] / 255.0f;
            if (a > 0.03f) { colOcc[x] = 1; rowOcc[y] = 1; }
            colSum[x] += a;
            rowSum[y] += a;
        }
    auto axis = [](const std::vector<float>& occ, const std::vector<float>& sum) {
        int len = (int)occ.size();
        int n = GapCount(occ, false);
        if (n > 1) return len / n;
        int p = ThinBoundarySize(sum);
        return p > 0 ? p : len;
    };
    int fw = axis(colOcc, colSum), fh = axis(rowOcc, rowSum);
    // A horizontal strip whose frames touch everywhere: square frames.
    if (fw == w && fh == h && w > h && w % h == 0) fw = h;
    return { fw, fh };
}

ivec2 DetectFrameSize(const std::string& path) {
    // Cached until anything reloads (AssetManager's version changes when a file changes).
    static std::map<std::string, ivec2> cache;
    static uint32_t version = 0;
    if (version != AssetManager::Version()) { cache.clear(); version = AssetManager::Version(); }
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    fs::path full = Project::Resolve(path);
    int w = 0, h = 0, c = 0;
    uint8_t* px = stbi_load(full.string().c_str(), &w, &h, &c, 4);
    ivec2 size = px ? DetectFrameSize(px, w, h) : ivec2(0);
    if (px) stbi_image_free(px);
    cache[path] = size;
    return size;
}
} // namespace SpriteSheet

} // namespace eu
