#pragma once
#include "euphoria/core/Base.h"
#include "euphoria/core/FileSystem.h"
#include <string>

namespace eu {

// Opens files only Blender reads well - .blend scenes, Collada (.dae), .3ds, .stl, .ply, .usd,
// Alembic - by running the Blender you have installed in the background to export a glTF,
// which the engine then loads like any other model. Nothing to set up: Blender is found
// automatically (or set EUPHORIA_BLENDER / the editor's Blender field), conversions run in the
// background while the editor keeps going, and saving the .blend re-exports it.
//
// Converted files are kept in <project>/.euphoria/converted/ (beside the source for files
// outside a project), are reused while they're newer than the source, and ship with exported
// games, so players never need Blender.
class BlenderBridge {
public:
    static bool Handles(const std::string& path);           // by extension

    // Path to blender(.exe), or "" when it can't be found.
    static std::string BlenderPath();
    static void SetBlenderPath(const std::string& path);    // remembered for this user

    enum class Status { Ready, Converting, Failed, NoBlender };
    // The converted .glb for `source`. Starts a background conversion when it's missing or
    // older than the source and returns Converting until it's done.
    static Status Request(const fs::path& source, fs::path& glb, std::string* message = nullptr);
    // Same, but waits for the conversion (command-line tools).
    static Status ConvertNow(const fs::path& source, fs::path& glb, std::string* message = nullptr);
    // True once after any background conversion finishes (the asset manager reloads then).
    static bool PollFinished();
    static bool Busy();

    static fs::path CachePathFor(const fs::path& source);
};

} // namespace eu
