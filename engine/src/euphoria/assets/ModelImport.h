#pragma once
// Internal helpers shared by the model loaders (glTF, FBX, OBJ). Not part of the public API.
#include "euphoria/assets/Model.h"
#include "euphoria/core/FileSystem.h"

namespace eu::ModelImport {

// Decodes an image file, shared between every model that uses it (a 180-piece building kit
// that all points at one 4K brick texture loads it once). Images are decoded without a vertical
// flip; loaders whose UVs have a bottom-left origin (FBX, OBJ) flip V on the mesh instead.
Ref<Texture2D> TextureFromFile(const std::string& absolutePath);
void ClearTextureCache();

// Resolves a texture reference from a model file (see TextureFinder) and loads it. Notes what
// happened in md.textureNotes ("albedo: Textures/Axe.png", "normal: missing Axe_N.tga").
// Returns the absolute path that was used (empty if none) through `usedPath`.
Ref<Texture2D> Resolve(const std::string& reference, const fs::path& modelDir, MaterialData& md, const char* slot,
                       std::string* usedPath = nullptr, bool noteMissing = true);

// Fills maps the file didn't specify from files named after the color texture
// (X_n / X_Normal, X_Emissor, X_ORM, X_Roughness, X_Metallic, X_AO).
void AddCompanionMaps(MaterialData& md, const std::string& colorPath);

// Applies <model file>.meta import settings (scale / rotation) and fills in derived data
// (bounds). Called by ModelLoader::Load after the format loader.
void ApplyImportSettings(Model& model, const std::string& modelPath);

// "Armature|Run" -> "Run"; "mixamo.com" / "Take 001" (exporter defaults) -> the file's name.
// Merges identical vertices (FBX and OBJ give three per triangle) and rewrites the indices.
void Weld(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

std::string CleanClipName(const std::string& name, const std::string& modelPath, size_t clipCount);


} // namespace eu::ModelImport
