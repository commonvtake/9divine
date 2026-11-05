#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/core/FileSystem.h"
#include <string>

namespace eu {

// Finds the image files a model refers to. Asset packs rarely keep the paths the artist's
// exporter wrote: an FBX may point at "C:\Users\artist\Desktop\char_tex.png", a glTF at
// "T_Hair_Normal_png.png" when the file is "T_Hair_Normal.png", and textures usually live in a
// "Textures" folder next to (or one level above) the "Models" folder. Find() tries, in order:
//   1. the path as written (absolute, or relative to the model's folder)
//   2. the file name (any letter case, with the extension swapped for one we can load, with an
//      exporter's "_png" / ".png.png" doubling removed) in the model's folder, a Textures /
//      textures / Texture / Materials folder beside it, its parent folder and the parent's
//      Textures folder, and two levels up
//   3. the same names anywhere below the model's grandparent folder (bounded scan)
// The result is an absolute path, or empty when nothing matches.
namespace TextureFinder {

std::string Find(const std::string& reference, const fs::path& modelDir);

// Other maps that go with a color texture, by the naming conventions asset packs use:
//   Barrel.png -> Barrel_n.png / Barrel_normal.png / Barrel_Normal.png / Barrel_nrm.png,
//                 Barrel_Emissor.png / Barrel_emissive.png / Barrel_Emission.png / Barrel_e.png
//   T_Brick_BaseColor.png -> T_Brick_Normal.png, T_Brick_ORM.png, T_Brick_Roughness.png, ...
struct Companions {
    std::string normal, emissive, orm, roughness, metallic, occlusion;
};
Companions FindCompanions(const std::string& colorTexturePath);

// Image formats the engine can decode (stb_image).
bool IsLoadableImage(const std::string& path);

// Forget cached folder listings (call when files may have been added).
void ClearCache();

} // namespace TextureFinder
} // namespace eu
