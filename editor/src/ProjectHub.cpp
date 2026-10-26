// Project Hub: the start screen (recent projects, New Project, Open Project) and
// the project switching logic shared with the editor's File menu.
#include "EditorApp.h"
#include "ProjectHub.h"
#include "ProjectTemplates.h"
#include <nd/assets/AssetManager.h>
#include <nd/core/Json.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <ctime>
#include <chrono>

namespace nd::editor {

// ======================================================================== helpers
bool IsProjectFolder(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "project.json", ec);
}

static int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string FormatLastOpened(int64_t seconds) {
    if (seconds <= 0) return "";
    int64_t ago = NowSeconds() - seconds;
    if (ago < 60) return "just now";
    if (ago < 3600) return std::to_string(ago / 60) + " min ago";
    if (ago < 86400) return std::to_string(ago / 3600) + " h ago";
    if (ago < 2 * 86400) return "yesterday";
    std::time_t t = (std::time_t)seconds;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%b %d, %Y", std::localtime(&t));
    return buf;
}

static std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// ======================================================================== RecentProjects
static fs::path RecentFile() { return Platform::UserDataDir("Editor") / "recent_projects.json"; }

void RecentProjects::Load() {
    m_List.clear();
    auto text = FileSystem::ReadText(RecentFile());
    if (!text) return;
    try {
        for (auto& j : json::parse(*text)) {
            RecentProject r;
            r.path = JGet<std::string>(j, "path", "");
            r.name = JGet<std::string>(j, "name", "");
            r.lastOpened = JGet<int64_t>(j, "lastOpened", 0);
            if (!r.path.empty()) m_List.push_back(r);
        }
    } catch (...) {}
}

void RecentProjects::Save() const {
    json arr = json::array();
    for (auto& r : m_List) arr.push_back({ { "path", r.path }, { "name", r.name }, { "lastOpened", r.lastOpened } });
    FileSystem::WriteText(RecentFile(), arr.dump(2));
}

void RecentProjects::Add(const fs::path& dir, const std::string& name) {
    std::string p = fs::absolute(dir).lexically_normal().generic_string();
    Remove(p);
    m_List.insert(m_List.begin(), { p, name, NowSeconds() });
    if (m_List.size() > 20) m_List.resize(20);
    Save();
}

void RecentProjects::Remove(const std::string& path) {
    m_List.erase(std::remove_if(m_List.begin(), m_List.end(), [&](const RecentProject& r) { return r.path == path; }), m_List.end());
    Save();
}

// ======================================================================== FolderBrowser
void FolderBrowser::Open(const fs::path& start) {
    std::error_code ec;
    fs::path dir = start;
    while (!dir.empty() && !fs::is_directory(dir, ec) && dir.has_parent_path() && dir.parent_path() != dir) dir = dir.parent_path();
    if (dir.empty() || !fs::is_directory(dir, ec)) dir = Platform::DocumentsDir();
    m_Result.clear();
    Navigate(dir);
}

void FolderBrowser::Navigate(const fs::path& dir) {
    std::error_code ec;
    fs::path abs = fs::absolute(dir, ec).lexically_normal();
    if (!fs::is_directory(abs, ec)) return;
    m_Dir = abs;
    m_PathText = m_Dir.string();
    m_Selected = -1;
    Refresh();
}

void FolderBrowser::Refresh() {
    m_Entries.clear();
    std::error_code ec;
    auto it = fs::directory_iterator(m_Dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_directory(ec2)) continue;
        std::string name = it->path().filename().string();
        if (name.empty() || name[0] == '.' || name[0] == '$') continue;
        m_Entries.push_back({ name, IsProjectFolder(it->path()) });
        if (m_Entries.size() > 3000) break;
    }
    std::sort(m_Entries.begin(), m_Entries.end(), [](const Entry& a, const Entry& b) { return Lower(a.name) < Lower(b.name); });
}

bool FolderBrowser::Draw(const char* popupId, const char* confirmLabel, bool requireProject) {
    bool confirmed = false;
    ImGui::SetNextWindowSize(ImVec2(680, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_NoSavedSettings)) return false;

    // Path bar
    if (ImGui::Button("Up") && m_Dir.has_parent_path() && m_Dir.parent_path() != m_Dir) Navigate(m_Dir.parent_path());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##path", &m_PathText, ImGuiInputTextFlags_EnterReturnsTrue)) Navigate(fs::path(m_PathText));

    // Shortcuts
    if (ImGui::SmallButton("Home")) Navigate(Platform::HomeDir());
    ImGui::SameLine();
    if (ImGui::SmallButton("Documents")) Navigate(Platform::DocumentsDir());
    fs::path desktop = Platform::HomeDir() / "Desktop";
    std::error_code ec;
    if (fs::is_directory(desktop, ec)) { ImGui::SameLine(); if (ImGui::SmallButton("Desktop")) Navigate(desktop); }
#ifdef _WIN32
    for (char d = 'A'; d <= 'Z'; d++) {
        std::string root = std::string(1, d) + ":\\";
        if (!fs::exists(root, ec)) continue;
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(1, d) + ":").c_str())) Navigate(root);
    }
#else
    ImGui::SameLine();
    if (ImGui::SmallButton("/")) Navigate("/");
#endif

    // Folder list
    float footer = ImGui::GetFrameHeightWithSpacing() * 2.6f;
    ImGui::BeginChild("##folders", ImVec2(0, -footer), true);
    if (m_Entries.empty()) ImGui::TextDisabled("(no sub-folders)");
    fs::path enter;
    for (int i = 0; i < (int)m_Entries.size(); i++) {
        auto& e = m_Entries[i];
        ImGui::PushID(i);
        if (e.isProject) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.9f, 0.7f, 1.0f));
        std::string label = (e.isProject ? "[9divine project]  " : "") + e.name;
        if (ImGui::Selectable(label.c_str(), m_Selected == i, ImGuiSelectableFlags_AllowDoubleClick)) {
            m_Selected = i;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (requireProject && e.isProject) { m_Result = m_Dir / e.name; confirmed = true; }
                else enter = m_Dir / e.name;
            }
        }
        if (e.isProject) ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!enter.empty()) Navigate(enter);

    // Footer
    fs::path candidate = (m_Selected >= 0 && m_Selected < (int)m_Entries.size()) ? m_Dir / m_Entries[m_Selected].name : m_Dir;
    bool ok = !requireProject || IsProjectFolder(candidate);
    if (requireProject) {
        if (ok) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.7f, 1.0f), "Project: %s", candidate.filename().string().c_str());
        else ImGui::TextDisabled("Select a folder that contains a project.json (shown in green).");
    } else {
        ImGui::TextDisabled("Folder: %s", candidate.string().c_str());
    }
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button(confirmLabel, ImVec2(120, 0))) { m_Result = candidate; confirmed = true; }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    if (confirmed) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return confirmed;
}

// ======================================================================== project switching
void EditorApp::RequestProjectChange(std::function<void()> action) {
    if (m_ProjectOpen && m_Dirty) {
        m_AfterUnsavedPrompt = std::move(action);
        m_ConfirmQuit = true;              // shows the "Unsaved Changes" prompt
    } else {
        m_PendingAction = std::move(action);
    }
}

bool EditorApp::OpenProject(const fs::path& dir) {
    if (m_State == PlayState::Play) StopPlay();
    if (!IsProjectFolder(dir) || !Project::Load(dir)) {
        m_ProjectError = "No 9divine project was found in:\n" + dir.string();
        m_ShowProjectError = true;
        // Keep the previously open project active
        if (m_ProjectOpen) Project::Load(m_ProjectDir);
        return false;
    }
    Audio::StopAll();
    AssetManager::ClearCache();
    Input::Map().SetDefaults();
    Input::LoadMap(Project::Info().inputMap);
    m_ProjectDir = Project::Root();
    m_AssetDir = Project::Root();
    m_AssetFilter.clear();
    m_BuildDir = (Project::Root().parent_path() / "Builds" / Project::Info().name).generic_string();
    m_SelectedID = 0;
    m_EditorScene.reset();
    m_Camera = EditorCamera();
    m_Sculpt = false;
    m_ProjectOpen = true;
    std::string start = Project::Info().startScene;
    if (start.empty() || !OpenScene(start)) NewScene();
    m_Recent.Add(Project::Root(), Project::Info().name);
    ND_INFO("Opened project '{}' ({})", Project::Info().name, Project::Root().string());
    UpdateTitle();
    return true;
}

void EditorApp::CloseProject() {
    if (m_State == PlayState::Play) StopPlay();
    Audio::StopAll();
    m_EditorScene.reset();
    m_SelectedID = 0;
    m_Dirty = false;
    m_ProjectOpen = false;
    m_Recent.Load();
    UpdateTitle();
}

void EditorApp::BeginNewProject() {
    m_NewProjectName = "MyGame";
    if (m_NewProjectLocation.empty()) m_NewProjectLocation = (Platform::DocumentsDir() / "9divine Projects").string();
    // Pick a name that doesn't exist yet
    for (int i = 2; fs::exists(fs::path(m_NewProjectLocation) / m_NewProjectName) && i < 100; i++) m_NewProjectName = "MyGame" + std::to_string(i);
    m_NewProjectTemplate = 1;   // 3D Starter
    m_OpenNewProjectDialog = true;
}

void EditorApp::BeginOpenProject() {
    fs::path start = m_ProjectOpen ? Project::Root().parent_path() : fs::path(m_NewProjectLocation.empty() ? Platform::DocumentsDir().string() : m_NewProjectLocation);
    m_OpenBrowser.Open(start);
    m_OpenOpenProjectDialog = true;
}

// ======================================================================== dialogs
void EditorApp::DrawProjectDialogs() {
    if (m_OpenNewProjectDialog) { ImGui::OpenPopup("New Project"); m_OpenNewProjectDialog = false; }
    if (m_OpenOpenProjectDialog) { ImGui::OpenPopup("Open Project"); m_OpenOpenProjectDialog = false; }
    if (m_ShowProjectError) { ImGui::OpenPopup("Project Error"); m_ShowProjectError = false; }

    ImGuiIO& io = ImGui::GetIO();
    ImFont* heading = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();

    // ---- New Project
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (heading) ImGui::PushFont(heading);
        ImGui::TextUnformatted("Choose a template");
        if (heading) ImGui::PopFont();
        ImGui::Spacing();

        const auto& list = templates::All();
        float total = std::max(720.0f, ImGui::GetContentRegionAvail().x);
        float cardW = (total - ImGui::GetStyle().ItemSpacing.x * (list.size() - 1)) / list.size();
        for (int i = 0; i < (int)list.size(); i++) {
            bool available = list[i].id != templates::Template::Sample || templates::SampleAvailable();
            bool selected = m_NewProjectTemplate == i;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, selected ? ImVec4(0.17f, 0.36f, 0.29f, 1.0f) : ImVec4(0.15f, 0.16f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.33f, 0.72f, 0.56f, 1.0f) : ImVec4(0.25f, 0.26f, 0.29f, 1.0f));
            ImGui::BeginChild("##card", ImVec2(cardW, 120), true, ImGuiWindowFlags_NoScrollbar);
            if (!available) ImGui::BeginDisabled();
            ImGui::TextUnformatted(list[i].name);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", available ? list[i].description : "Not available in this build.");
            ImGui::PopStyleColor();
            if (!available) ImGui::EndDisabled();
            if (available && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_NewProjectTemplate = i;
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            if (i + 1 < (int)list.size()) ImGui::SameLine();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Project name");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##pname", &m_NewProjectName);
        ImGui::Text("Location");
        ImGui::SetNextItemWidth(-100);
        ImGui::InputText("##ploc", &m_NewProjectLocation);
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
            m_LocationBrowser.Open(m_NewProjectLocation);
            ImGui::OpenPopup("Choose Location");
        }
        if (m_LocationBrowser.Draw("Choose Location", "Select", false)) m_NewProjectLocation = m_LocationBrowser.Result().string();

        // Validation
        std::string why;
        fs::path target = fs::path(m_NewProjectLocation) / m_NewProjectName;
        bool valid = templates::IsValidProjectName(m_NewProjectName, &why);
        std::error_code ec;
        if (valid && m_NewProjectLocation.empty()) { valid = false; why = "Choose a location."; }
        if (valid && fs::exists(target, ec) && !(fs::is_directory(target, ec) && fs::is_empty(target, ec))) {
            valid = false;
            why = "A folder with that name already exists here.";
        }
        ImGui::Spacing();
        if (valid) ImGui::TextDisabled("Creates: %s", target.string().c_str());
        else ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", why.c_str());
        ImGui::Spacing();

        ImGui::BeginDisabled(!valid);
        bool create = ImGui::Button("Create Project", ImVec2(160, 0)) || (valid && ImGui::IsKeyPressed(ImGuiKey_Enter) && !ImGui::IsPopupOpen("Choose Location"));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        if (create && valid) {
            auto tmpl = list[m_NewProjectTemplate].id;
            std::string name = m_NewProjectName;
            RequestProjectChange([this, target, name, tmpl] {
                fs::path previous = m_ProjectOpen ? m_ProjectDir : fs::path();
                std::string error;
                if (!templates::CreateProject(target, name, tmpl, error)) {
                    if (!previous.empty()) Project::Load(previous);
                    m_ProjectError = "Couldn't create the project:\n" + error;
                    m_ShowProjectError = true;
                    return;
                }
                OpenProject(target);
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ---- Open Project
    if (m_OpenBrowser.Draw("Open Project", "Open", true)) {
        fs::path dir = m_OpenBrowser.Result();
        RequestProjectChange([this, dir] { OpenProject(dir); });
    }

    // ---- Errors
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(m_ProjectError.c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ======================================================================== hub screen
void EditorApp::DrawProjectHub() {
    ImGuiIO& io = ImGui::GetIO();
    ImFont* heading = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;
    ImFont* title = io.Fonts->Fonts.Size > 2 ? io.Fonts->Fonts[2] : nullptr;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##hub", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar(2);

    // ---- Sidebar
    const float sidebar = 320.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.085f, 0.09f, 0.10f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 32));
    ImGui::BeginChild("##sidebar", ImVec2(sidebar, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    if (title) ImGui::PushFont(title);
    ImGui::TextColored(ImVec4(0.42f, 0.85f, 0.66f, 1.0f), "9divine");
    if (title) ImGui::PopFont();
    ImGui::TextDisabled("Game Engine  v0.1");
    ImGui::Dummy(ImVec2(0, 30));

    ImVec2 big(-1, 46);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.52f, 0.41f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.62f, 0.48f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.19f, 0.45f, 0.36f, 1.0f));
    if (ImGui::Button("New Project", big)) BeginNewProject();
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
    if (ImGui::Button("Open Project...", big)) BeginOpenProject();
    ImGui::Spacing();
    bool sample = templates::SampleAvailable();
    ImGui::BeginDisabled(!sample);
    if (ImGui::Button("Open Sample: Ember Isle", big)) {
        fs::path dir = fs::path(ND_SOURCE_DIR) / "projects" / "Demo";
        RequestProjectChange([this, dir] { OpenProject(dir); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(sample ? "Opens the bundled demo in place.\nUse New Project > Ember Isle to get your own copy." : "Sample not included in this build");

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 110);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Projects are folders with a project.json, scenes and assets. Gameplay scripts live in game/src and are shared by every project.");
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ---- Recent projects
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36, 32));
    ImGui::BeginChild("##recent", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    if (heading) ImGui::PushFont(heading);
    ImGui::TextUnformatted("Recent Projects");
    if (heading) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 36 - 260);
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##search", "Search projects", &m_HubSearch);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const auto& list = m_Recent.List();
    if (list.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::TextDisabled("No recent projects yet.");
        ImGui::TextDisabled("Click New Project to create one, or Open Project to add an existing folder.");
    }
    std::string removePath;
    std::string filter = Lower(m_HubSearch);
    for (size_t i = 0; i < list.size(); i++) {
        const RecentProject& r = list[i];
        if (!filter.empty() && Lower(r.name).find(filter) == std::string::npos && Lower(r.path).find(filter) == std::string::npos) continue;
        bool exists = IsProjectFolder(r.path);
        ImGui::PushID((int)i);
        ImVec2 rowPos = ImGui::GetCursorScreenPos();
        float rowW = ImGui::GetContentRegionAvail().x;
        if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(rowW, 52)) && exists) {
            fs::path dir = r.path;
            RequestProjectChange([this, dir] { OpenProject(dir); });
        }
        if (ImGui::BeginPopupContextItem("##ctx")) {
            if (ImGui::MenuItem("Open", nullptr, false, exists)) { fs::path dir = r.path; RequestProjectChange([this, dir] { OpenProject(dir); }); }
            if (ImGui::MenuItem("Show in folder", nullptr, false, exists)) Platform::OpenInFileBrowser(r.path);
            if (ImGui::MenuItem("Remove from list")) removePath = r.path;
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(exists ? "Click to open  -  right-click for more" : "This folder no longer exists (right-click to remove)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 nameCol = exists ? IM_COL32(235, 238, 242, 255) : IM_COL32(150, 150, 150, 255);
        dl->AddText(heading ? heading : ImGui::GetFont(), heading ? heading->FontSize * 0.82f : ImGui::GetFontSize(), ImVec2(rowPos.x + 12, rowPos.y + 6),
                    nameCol, (r.name + (exists ? "" : "   (missing)")).c_str());
        dl->AddText(ImVec2(rowPos.x + 12, rowPos.y + 30), IM_COL32(130, 136, 145, 255), r.path.c_str());
        std::string when = FormatLastOpened(r.lastOpened);
        float tw = ImGui::CalcTextSize(when.c_str()).x;
        dl->AddText(ImVec2(rowPos.x + rowW - tw - 12, rowPos.y + 18), IM_COL32(130, 136, 145, 255), when.c_str());
        ImGui::PopID();
    }
    if (!removePath.empty()) m_Recent.Remove(removePath);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}

} // namespace nd::editor
