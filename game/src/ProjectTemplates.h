#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Templates used by the editor's Project Hub ("New Project") and by
// `9divine-editor --create-project <dir> --template <name>`.
namespace templates {

enum class Template { Empty, Starter3D, Sample };

struct TemplateInfo {
    Template id;
    const char* key;          // command-line name
    const char* name;         // shown in the hub
    const char* description;
};

const std::vector<TemplateInfo>& All();
bool FromKey(const std::string& key, Template& out);
bool SampleAvailable();      // the Ember Isle sample ships with the source tree

// Letters, digits, spaces, '-' and '_' (it becomes a folder name).
bool IsValidProjectName(const std::string& name, std::string* why = nullptr);

// Creates <dir> (which must not exist or be empty) and fills it from the template.
// Leaves the new project loaded (nd::Project) on success.
bool CreateProject(const std::filesystem::path& dir, const std::string& name, Template t, std::string& error);

} // namespace templates
