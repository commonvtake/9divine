#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/assets/Model.h"
#include <map>

namespace eu {

// Per-model import settings, stored next to the model as <file>.meta (JSON).
struct ModelImportSettings {
    float scale = 1.0f;         // e.g. 0.01 for a centimetre export that wasn't converted
    vec3 rotation{ 0.0f };      // degrees, e.g. (-90, 0, 0) for a Z-up export
    bool faceForward = true;    // turn humanoid characters to face +Z (like glTF), whatever the exporter did
};

// Loads and caches assets by path. Paths are project-relative ("Models/fox.glb"),
// "engine:..." for engine assets, or "builtin:cube" style primitives.
// Supported models: glTF/GLB and FBX (meshes, materials, skins, animations), OBJ, and through
// an installed Blender: .blend, .dae, .3ds, .stl, .ply, .usd, Alembic (see BlenderBridge).
// "Models/Kit.fbx#Barrel" is the object named Barrel inside Kit.fbx, on its own pivot.
// Textures: PNG/JPG/TGA/BMP/PSD/GIF. Sounds are cached by the audio engine.
struct TilemapData;

class AssetManager {
public:
    static Ref<Model> GetModel(const std::string& path);
    // pixelArt: nearest-neighbour filtering, no mipmaps (cached separately from the smooth version)
    static Ref<Texture2D> GetTexture(const std::string& path, bool pixelArt = false);
    // A Tiled map (.tmx/.tmj); reloads when the map or its tilesets change. nullptr if it can't load.
    static Ref<TilemapData> GetTilemap(const std::string& path);

    // Hot reload: checks file timestamps (call every frame; it throttles itself).
    static void PollChanges();
    static void ClearCache();
    static uint32_t Version() { return s_Version; }   // bumps whenever something reloads

    // `source`'s animations retargeted onto `target`'s skeleton (cached; see Retarget.h).
    static const std::vector<Ref<AnimationClip>>& RetargetedClips(const Model& target, const std::string& source);

    static ModelImportSettings LoadImportSettings(const std::string& modelPath);
    static void SaveImportSettings(const std::string& modelPath, const ModelImportSettings& settings);   // and reload

    static bool IsModelFile(const std::string& path);
    static bool IsTextureFile(const std::string& path);
    static bool IsAudioFile(const std::string& path);
    static bool IsSceneFile(const std::string& path);
    static bool IsTilemapFile(const std::string& path);

    struct Stats { size_t models = 0, textures = 0; };
    static Stats GetStats() { return { s_Models.size(), s_Textures.size() }; }

private:
    struct ModelEntry { Ref<Model> model; int64_t stamp = 0; };
    struct TextureEntry { Ref<Texture2D> texture; int64_t stamp = 0; bool pixelArt = false; };
    static std::map<std::string, ModelEntry> s_Models;
    static std::map<std::string, TextureEntry> s_Textures;   // key: path, or path + "\n" + "px" for pixel art
    struct TilemapEntry { Ref<TilemapData> map; std::vector<std::pair<std::string, int64_t>> stamps; };
    static std::map<std::string, TilemapEntry> s_Tilemaps;
    static std::map<std::string, std::vector<Ref<AnimationClip>>> s_Retargeted;
    static uint32_t s_Version;
};

namespace ModelLoader {
Ref<Model> Load(const std::string& absolutePath);
Ref<Model> LoadGLTF(const std::string& absolutePath);
Ref<Model> LoadOBJ(const std::string& absolutePath);
Ref<Model> LoadFBX(const std::string& absolutePath);
// The object named `object` from a loaded model, moved to its pivot (see Model).
Ref<Model> ExtractObject(const Model& whole, const std::string& object);
// A readable summary: format, size, objects, materials and their textures (found or missing),
// skeleton and humanoid mapping, animations. `euphoria-player --model-info <file>` prints it.
std::string Describe(const Model& model);
}

} // namespace eu
