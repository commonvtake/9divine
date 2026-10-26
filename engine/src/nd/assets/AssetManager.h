#pragma once
#include "nd/core/Base.h"
#include "nd/assets/Model.h"
#include <map>

namespace nd {

// Loads and caches assets by path. Paths are project-relative ("Models/fox.glb"),
// "engine:..." for engine assets, or "builtin:cube" style primitives.
// Supported: glTF/GLB (meshes, materials, skins, animations), OBJ, FBX (static meshes),
// PNG/JPG/TGA/BMP textures. Sounds are cached by the audio engine.
class AssetManager {
public:
    static Ref<Model> GetModel(const std::string& path);
    static Ref<Texture2D> GetTexture(const std::string& path, bool pixelArt = false);

    // Hot reload: checks file timestamps (call every frame; it throttles itself).
    static void PollChanges();
    static void ClearCache();
    static uint32_t Version() { return s_Version; }   // bumps whenever something reloads

    static bool IsModelFile(const std::string& path);
    static bool IsTextureFile(const std::string& path);
    static bool IsAudioFile(const std::string& path);
    static bool IsSceneFile(const std::string& path);

    struct Stats { size_t models = 0, textures = 0; };
    static Stats GetStats() { return { s_Models.size(), s_Textures.size() }; }

private:
    struct ModelEntry { Ref<Model> model; int64_t stamp = 0; };
    struct TextureEntry { Ref<Texture2D> texture; int64_t stamp = 0; bool pixelArt = false; };
    static std::map<std::string, ModelEntry> s_Models;
    static std::map<std::string, TextureEntry> s_Textures;
    static uint32_t s_Version;
};

namespace ModelLoader {
Ref<Model> Load(const std::string& absolutePath);
Ref<Model> LoadGLTF(const std::string& absolutePath);
Ref<Model> LoadOBJ(const std::string& absolutePath);
Ref<Model> LoadFBX(const std::string& absolutePath);
}

} // namespace nd
