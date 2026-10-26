#pragma once
#include <filesystem>

// Generates the sample project (project.json, input map, Island + Arena scenes) from code.
// Run from the editor (Help > Rebuild Demo Scenes) or `9divine-player --build-demo <dir>`.
namespace demo {
bool BuildDemoProject(const std::filesystem::path& projectDir);
}
