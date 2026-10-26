#pragma once
// Project Hub helpers: recent-project list and an in-editor folder browser.
#include <nd/core/FileSystem.h>
#include <string>
#include <vector>
#include <cstdint>

namespace nd::editor {

struct RecentProject {
    std::string path;      // absolute, generic separators
    std::string name;
    int64_t lastOpened = 0; // seconds since epoch
};

// Persisted in the user's app-data folder (recent_projects.json).
class RecentProjects {
public:
    void Load();
    void Save() const;
    void Add(const fs::path& dir, const std::string& name);
    void Remove(const std::string& path);
    const std::vector<RecentProject>& List() const { return m_List; }

private:
    std::vector<RecentProject> m_List;
};

// Modal folder picker drawn with ImGui (works the same on every platform).
//   browser.Open(startDir); ImGui::OpenPopup("Pick Folder");
//   if (browser.Draw("Pick Folder", "Select", false)) use(browser.Result());
class FolderBrowser {
public:
    void Open(const fs::path& start);
    // Returns true on the frame the user confirms. When `requireProject` is set the
    // confirm button only enables for folders that contain a project.json.
    bool Draw(const char* popupId, const char* confirmLabel, bool requireProject);
    const fs::path& Result() const { return m_Result; }

private:
    struct Entry { std::string name; bool isProject = false; };
    void Navigate(const fs::path& dir);
    void Refresh();

    fs::path m_Dir, m_Result;
    std::string m_PathText;
    std::vector<Entry> m_Entries;
    int m_Selected = -1;
};

std::string FormatLastOpened(int64_t seconds);
bool IsProjectFolder(const fs::path& dir);

} // namespace nd::editor
