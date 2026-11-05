#pragma once
#include <filesystem>

// Generates the sample project (project.json, input map, City + interiors, Island, Arena) from code.
// Run from the editor (Help > Rebuild Demo Scenes) or `euphoria-player --build-demo <dir>`.
namespace demo {
bool BuildDemoProject(const std::filesystem::path& projectDir);
}
