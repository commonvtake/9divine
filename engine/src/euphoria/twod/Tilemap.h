#pragma once
// Tiled maps (https://www.mapeditor.org): .tmx / .tmj with external .tsx / .tsj tilesets.
//
// Supported: orthogonal maps (finite and infinite), tile layers (CSV, base64, base64+zlib/gzip,
// legacy <tile> elements), groups (opacity, tint, offsets, visibility and parallax inherit),
// image layers, object layers (tile objects are drawn; shapes and points are kept for scripts),
// atlas and image-collection tilesets, tile offsets, flipped/rotated tiles, animated tiles,
// per-tile collision shapes, and custom properties.
//
// Coordinates: Tiled's are pixels with y pointing down. TilemapData keeps those; the
// renderer and physics place the map's bottom-left corner at the entity (see MapToLocal).
#include "euphoria/core/Base.h"
#include <map>

namespace eu {

class Texture2D;

constexpr uint32_t kTileFlipH = 0x80000000u, kTileFlipV = 0x40000000u, kTileFlipD = 0x20000000u, kTileRotHex = 0x10000000u;
constexpr uint32_t kTileGidMask = 0x0FFFFFFFu;

using TiledProperties = std::map<std::string, std::string>;

struct TileRect { float x = 0, y = 0, w = 0, h = 0; };   // pixels, y down

struct TileFrame { int tile = 0; float duration = 0.1f; };   // seconds

struct TilesetTile {
    std::string image;                      // image-collection tilesets: this tile's own image
    int imageWidth = 0, imageHeight = 0;
    ivec4 subRect{ 0 };                     // collection tiles may use part of their image (x, y, w, h); w 0 = all
    std::vector<TileFrame> animation;
    float animationLength = 0.0f;
    std::vector<TileRect> colliders;        // in the tile's pixels (top-left origin)
    std::string type;                       // Tiled class
    TiledProperties properties;
};

struct Tileset {
    std::string name;
    uint32_t firstGid = 1;
    int tileWidth = 16, tileHeight = 16;
    int columns = 0, tileCount = 0, spacing = 0, margin = 0;
    std::string image;                      // atlas image (empty for image collections)
    int imageWidth = 0, imageHeight = 0;
    ivec2 tileOffset{ 0 };
    std::map<int, TilesetTile> tiles;       // tiles with extra data (or every tile of a collection)
    TiledProperties properties;

    bool IsCollection() const { return image.empty(); }
    const TilesetTile* Tile(int id) const { auto it = tiles.find(id); return it == tiles.end() ? nullptr : &it->second; }
    // The image and pixel rectangle (top-left origin) for local tile `id`. False if unknown.
    bool Source(int id, std::string& outImage, ivec4& outRect) const;
    // Local tile id shown at `timeSeconds` (follows the tile's animation, if any).
    int Animated(int id, float timeSeconds) const;
};

enum class TiledShape { Rectangle, Ellipse, Point, Polygon, Polyline, Tile, Text };
struct TilemapObject {
    int id = 0;
    std::string name, type;
    TiledShape shape = TiledShape::Rectangle;
    float x = 0, y = 0, width = 0, height = 0, rotation = 0;   // pixels; tile objects: (x, y) = bottom-left
    uint32_t gid = 0;                       // tile objects (with flip flags)
    std::vector<vec2> points;               // polygon / polyline, relative to (x, y)
    bool visible = true;
    TiledProperties properties;
};

enum class TiledLayerType { Tiles, Objects, Image };
struct TilemapLayer {
    TiledLayerType type = TiledLayerType::Tiles;
    std::string name, className;
    bool visible = true;
    float opacity = 1.0f;
    vec4 tint{ 1.0f };
    vec2 offset{ 0.0f };                    // pixels (group offsets included)
    vec2 parallax{ 1.0f };
    TiledProperties properties;
    // Tiles: gids for a width x height block whose top-left cell is (originX, originY).
    int originX = 0, originY = 0, width = 0, height = 0;
    std::vector<uint32_t> gids;
    // Objects
    std::vector<TilemapObject> objects;
    // Image layer
    std::string image;
    int imageWidth = 0, imageHeight = 0;
    // Drawing (decided on load, see TilemapData::AssignDrawOrder)
    int order = 0;
    bool ySort = false;
    bool collision = false;                 // every tile in this layer is solid

    uint32_t At(int cx, int cy) const {     // cell in map coordinates; 0 = empty
        int lx = cx - originX, ly = cy - originY;
        if (lx < 0 || ly < 0 || lx >= width || ly >= height) return 0;
        return gids[(size_t)ly * width + lx];
    }
};

struct TilemapData {
    std::string path;                       // project-relative or absolute path of the map file
    int width = 0, height = 0;              // in tiles (finite maps; infinite maps: the used area)
    int tileWidth = 16, tileHeight = 16;
    bool infinite = false;
    std::string orientation = "orthogonal";
    bool hasBackground = false;
    vec4 background{ 0.0f };
    ivec4 bounds{ 0 };                      // used tile area: x, y, w, h (cells)
    std::vector<Tileset> tilesets;
    std::vector<TilemapLayer> layers;       // groups flattened, in drawing order (bottom first)
    TiledProperties properties;
    std::vector<std::string> warnings;
    std::vector<std::string> dependencies;  // absolute paths of the external tilesets (for hot reload)
    std::vector<TileRect> colliders;        // merged collision rectangles (pixels), see BuildColliders

    const Tileset* FindTileset(uint32_t gid, int& localId) const;
    float PixelHeight() const { return (float)(bounds.y + bounds.w) * tileHeight; }   // bottom edge of the used area
    // Map pixels (y down) -> the tilemap entity's local units (y up, bottom-left at 0).
    vec2 MapToLocal(const vec2& px, float pixelsPerUnit) const { return { px.x / pixelsPerUnit, (PixelHeight() - px.y) / pixelsPerUnit }; }
    vec2 LocalToMap(const vec2& local, float pixelsPerUnit) const { return { local.x * pixelsPerUnit, PixelHeight() - local.y * pixelsPerUnit }; }

    const TilemapObject* FindObject(const std::string& name) const;
    void AssignDrawOrder();
    void BuildColliders();
};

namespace Tiled {
// Loads a .tmx or .tmj (and the tilesets/images it references). Image paths in the result
// are made project-relative where possible. Returns nullptr and sets `error` on failure.
Ref<TilemapData> Load(const std::string& absolutePath, std::string* error = nullptr);
Ref<TilemapData> LoadFromText(const std::string& text, bool json, const std::string& baseDir, std::string* error = nullptr);
bool IsMapFile(const std::string& path);

// Merges touching rectangles with matching edges into larger ones (fewer bodies, no seams to
// snag on). Exposed for tests.
std::vector<TileRect> MergeRects(std::vector<TileRect> rects);
}

struct TilemapComponent;
// Loads (or reloads, after the file changed) the component's map. nullptr if it has none.
TilemapData* EnsureTilemapLoaded(TilemapComponent& component);

// Runtime state of a TilemapComponent: the loaded map and its textures.
struct TilemapRuntime {
    Ref<TilemapData> data;
    std::map<std::string, Ref<Texture2D>> textures;
    Ref<Texture2D> Texture(const std::string& image);
};

// Sprite sheets ------------------------------------------------------------------
namespace SpriteSheet {
// Guesses the frame size of a sheet from its alpha channel: frames separated by transparent
// gaps, else touching frames found by where the silhouettes thin out, else square frames
// (a horizontal strip), else the whole image. `rgba` is top row first.
ivec2 DetectFrameSize(const uint8_t* rgba, int width, int height);
// Same for an image file (cached per path).
ivec2 DetectFrameSize(const std::string& texturePath);
}

} // namespace eu
