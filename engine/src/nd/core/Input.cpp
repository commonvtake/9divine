#include "nd/core/Input.h"
#include "nd/core/FileSystem.h"
#include "nd/core/Log.h"
#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>

namespace nd {

GLFWwindow* Input::s_Window = nullptr;
bool Input::s_GameEnabled = true;
bool Input::s_Keys[Key::Last + 1] = {}, Input::s_PrevKeys[Key::Last + 1] = {};
bool Input::s_Mouse[Mouse::Last + 1] = {}, Input::s_PrevMouse[Mouse::Last + 1] = {};
bool Input::s_Pad[4][Gamepad::ButtonCount] = {}, Input::s_PrevPad[4][Gamepad::ButtonCount] = {};
float Input::s_PadAxes[4][Gamepad::AxisCount] = {};
bool Input::s_PadConnected[4] = {};
vec2 Input::s_MousePos{ 0.0f }, Input::s_MouseDelta{ 0.0f }, Input::s_LastMousePos{ 0.0f };
float Input::s_Scroll = 0.0f;
bool Input::s_FirstMouse = true;
CursorMode Input::s_CursorMode = CursorMode::Normal;
InputMap Input::s_Map;
Input::RebindState Input::s_Rebind;

static std::vector<int> BuildValidKeys() {
    std::vector<int> k = { 32, 39, 59, 61, 96, 161, 162 };
    auto range = [&](int a, int b) { for (int i = a; i <= b; i++) k.push_back(i); };
    range(44, 57); range(65, 93); range(256, 269); range(280, 284); range(290, 314); range(320, 336); range(340, 348);
    return k;
}
static const std::vector<int>& ValidKeys() { static std::vector<int> keys = BuildValidKeys(); return keys; }

struct InputCallbacks {
    static void Scroll(GLFWwindow*, double, double y) { Input::s_Scroll += (float)y; }
};

void Input::Init(GLFWwindow* window) {
    s_Window = window;
    glfwSetScrollCallback(window, InputCallbacks::Scroll);
    if (s_Map.actions.empty() && s_Map.axes.empty()) s_Map.SetDefaults();
}

void Input::BeginFrame() {
    std::memcpy(s_PrevKeys, s_Keys, sizeof(s_Keys));
    std::memcpy(s_PrevMouse, s_Mouse, sizeof(s_Mouse));
    std::memcpy(s_PrevPad, s_Pad, sizeof(s_Pad));
    s_Scroll = 0.0f;
}

void Input::EndFrame() {
    if (!s_Window) return;
    for (int k : ValidKeys()) s_Keys[k] = glfwGetKey(s_Window, k) == GLFW_PRESS;
    for (int b = 0; b <= Mouse::Last; b++) s_Mouse[b] = glfwGetMouseButton(s_Window, b) == GLFW_PRESS;

    double x, y;
    glfwGetCursorPos(s_Window, &x, &y);
    s_MousePos = { (float)x, (float)y };
    if (s_FirstMouse) { s_LastMousePos = s_MousePos; s_FirstMouse = false; }
    s_MouseDelta = s_MousePos - s_LastMousePos;
    s_LastMousePos = s_MousePos;

    for (int p = 0; p < 4; p++) {
        GLFWgamepadstate state;
        s_PadConnected[p] = glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + p) && glfwGetGamepadState(GLFW_JOYSTICK_1 + p, &state);
        if (!s_PadConnected[p]) {
            std::memset(s_Pad[p], 0, sizeof(s_Pad[p]));
            std::memset(s_PadAxes[p], 0, sizeof(s_PadAxes[p]));
            continue;
        }
        for (int b = 0; b < Gamepad::ButtonCount; b++) s_Pad[p][b] = state.buttons[b] == GLFW_PRESS;
        for (int a = 0; a < Gamepad::AxisCount; a++) s_PadAxes[p][a] = state.axes[a];
    }

    // Rebinding capture
    if (s_Rebind.active) {
        if (s_Keys[Key::Escape] && !s_PrevKeys[Key::Escape]) { s_Rebind.active = false; return; }
        for (int k : ValidKeys())
            if (s_Keys[k] && !s_PrevKeys[k]) { FinishRebind({ BindingType::Key, k, s_Rebind.scale }); return; }
        for (int b = 0; b <= Mouse::Last; b++)
            if (s_Mouse[b] && !s_PrevMouse[b]) { FinishRebind({ BindingType::MouseButton, b, s_Rebind.scale }); return; }
        for (int b = 0; b < Gamepad::ButtonCount; b++)
            if (s_Pad[0][b] && !s_PrevPad[0][b]) { FinishRebind({ BindingType::GamepadButton, b, s_Rebind.scale }); return; }
        for (int a = 0; a < Gamepad::AxisCount; a++) {
            float v = s_PadAxes[0][a];
            bool trigger = a >= Gamepad::LeftTrigger;
            if (trigger ? v > 0.5f : std::abs(v) > 0.7f) {
                float sign = trigger ? 1.0f : (v > 0 ? 1.0f : -1.0f);
                FinishRebind({ BindingType::GamepadAxis, a, sign * std::abs(s_Rebind.scale) });
                return;
            }
        }
    }
}

void Input::FinishRebind(const InputBinding& b) {
    std::vector<InputBinding>* list = nullptr;
    if (s_Rebind.isAxis) { if (auto* a = s_Map.FindAxis(s_Rebind.name)) list = &a->bindings; }
    else { if (auto* a = s_Map.FindAction(s_Rebind.name)) list = &a->bindings; }
    if (list) {
        if (s_Rebind.index >= 0 && s_Rebind.index < (int)list->size()) (*list)[s_Rebind.index] = b;
        else list->push_back(b);
        ND_INFO("Rebound '{}' to {}", s_Rebind.name, BindingName(b));
    }
    s_Rebind.active = false;
    // Swallow this frame's press so it doesn't also trigger gameplay.
    std::memcpy(s_PrevKeys, s_Keys, sizeof(s_Keys));
    std::memcpy(s_PrevMouse, s_Mouse, sizeof(s_Mouse));
    std::memcpy(s_PrevPad, s_Pad, sizeof(s_Pad));
}

void Input::BeginRebind(const std::string& name, bool isAxis, int index, float scale) {
    s_Rebind = { true, name, isAxis, index, scale };
}

bool Input::IsKeyDown(int key) { return s_GameEnabled && RawKeyDown(key); }
bool Input::IsKeyPressed(int key) { return s_GameEnabled && RawKeyPressed(key); }
bool Input::IsKeyReleased(int key) { return s_GameEnabled && key >= 0 && key <= Key::Last && !s_Keys[key] && s_PrevKeys[key]; }
bool Input::IsMouseDown(int b) { return s_GameEnabled && RawMouseDown(b); }
bool Input::IsMousePressed(int b) { return s_GameEnabled && b >= 0 && b <= Mouse::Last && s_Mouse[b] && !s_PrevMouse[b]; }
bool Input::IsMouseReleased(int b) { return s_GameEnabled && b >= 0 && b <= Mouse::Last && !s_Mouse[b] && s_PrevMouse[b]; }
vec2 Input::MousePosition() { return s_MousePos; }
vec2 Input::MouseDelta() { return s_GameEnabled ? s_MouseDelta : vec2(0.0f); }
float Input::ScrollDelta() { return s_GameEnabled ? s_Scroll : 0.0f; }

bool Input::RawKeyDown(int key) { return key >= 0 && key <= Key::Last && s_Keys[key]; }
bool Input::RawKeyPressed(int key) { return key >= 0 && key <= Key::Last && s_Keys[key] && !s_PrevKeys[key]; }
bool Input::RawMouseDown(int b) { return b >= 0 && b <= Mouse::Last && s_Mouse[b]; }

void Input::SetCursorMode(CursorMode mode) {
    s_CursorMode = mode;
    if (!s_Window) return;
    int m = mode == CursorMode::Normal ? GLFW_CURSOR_NORMAL : mode == CursorMode::Hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED;
    glfwSetInputMode(s_Window, GLFW_CURSOR, m);
    if (mode == CursorMode::Locked && glfwRawMouseMotionSupported()) glfwSetInputMode(s_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

bool Input::IsGamepadConnected(int pad) { return pad >= 0 && pad < 4 && s_PadConnected[pad]; }
bool Input::IsGamepadButtonDown(int b, int pad) { return s_GameEnabled && pad >= 0 && pad < 4 && b >= 0 && b < Gamepad::ButtonCount && s_Pad[pad][b]; }
bool Input::IsGamepadButtonPressed(int b, int pad) { return IsGamepadButtonDown(b, pad) && !s_PrevPad[pad][b]; }

float Input::GetGamepadAxis(int axis, int pad) {
    if (!s_GameEnabled || pad < 0 || pad >= 4 || axis < 0 || axis >= Gamepad::AxisCount) return 0.0f;
    float v = s_PadAxes[pad][axis];
    if (axis >= Gamepad::LeftTrigger) return (v + 1.0f) * 0.5f; // triggers: 0..1
    return std::abs(v) < 0.15f ? 0.0f : v;
}

std::string Input::GamepadName(int pad) {
    if (!IsGamepadConnected(pad)) return "";
    const char* n = glfwGetGamepadName(GLFW_JOYSTICK_1 + pad);
    return n ? n : "Gamepad";
}

// ---------------------------------------------------------------- Actions
bool Input::BindingDown(const InputBinding& b, const bool* keys, const bool* mouse, const bool* pads) {
    switch (b.type) {
    case BindingType::Key: return b.code >= 0 && b.code <= Key::Last && keys[b.code];
    case BindingType::MouseButton: return b.code >= 0 && b.code <= Mouse::Last && mouse[b.code];
    case BindingType::GamepadButton: return b.code >= 0 && b.code < Gamepad::ButtonCount && pads[b.code];
    case BindingType::GamepadAxis: {
        if (b.code < 0 || b.code >= Gamepad::AxisCount) return false;
        float v = s_PadAxes[0][b.code];
        if (b.code >= Gamepad::LeftTrigger) v = (v + 1.0f) * 0.5f;
        return v * (b.scale >= 0 ? 1.0f : -1.0f) > 0.5f;
    }
    default: return false;
    }
}

bool Input::GetAction(const std::string& name) {
    if (!s_GameEnabled || s_Rebind.active) return false;
    auto* a = s_Map.FindAction(name);
    if (!a) return false;
    for (auto& b : a->bindings) if (BindingDown(b, s_Keys, s_Mouse, s_Pad[0])) return true;
    return false;
}

bool Input::GetActionPressed(const std::string& name) {
    if (!s_GameEnabled || s_Rebind.active) return false;
    auto* a = s_Map.FindAction(name);
    if (!a) return false;
    bool now = false, before = false;
    for (auto& b : a->bindings) {
        now |= BindingDown(b, s_Keys, s_Mouse, s_Pad[0]);
        if (b.type != BindingType::GamepadAxis) before |= BindingDown(b, s_PrevKeys, s_PrevMouse, s_PrevPad[0]);
    }
    return now && !before;
}

bool Input::GetActionReleased(const std::string& name) {
    if (!s_GameEnabled) return false;
    auto* a = s_Map.FindAction(name);
    if (!a) return false;
    bool now = false, before = false;
    for (auto& b : a->bindings) {
        now |= BindingDown(b, s_Keys, s_Mouse, s_Pad[0]);
        before |= BindingDown(b, s_PrevKeys, s_PrevMouse, s_PrevPad[0]);
    }
    return !now && before;
}

float Input::BindingValue(const InputBinding& b, float deadzone) {
    switch (b.type) {
    case BindingType::Key: return (b.code >= 0 && b.code <= Key::Last && s_Keys[b.code]) ? b.scale : 0.0f;
    case BindingType::MouseButton: return (b.code >= 0 && b.code <= Mouse::Last && s_Mouse[b.code]) ? b.scale : 0.0f;
    case BindingType::GamepadButton: return (b.code >= 0 && b.code < Gamepad::ButtonCount && s_Pad[0][b.code]) ? b.scale : 0.0f;
    case BindingType::GamepadAxis: {
        if (b.code < 0 || b.code >= Gamepad::AxisCount) return 0.0f;
        float v = s_PadAxes[0][b.code];
        if (b.code >= Gamepad::LeftTrigger) v = (v + 1.0f) * 0.5f;
        if (std::abs(v) < deadzone) return 0.0f;
        return v * b.scale;
    }
    case BindingType::MouseAxis:
        if (b.code == 0) return s_MouseDelta.x * b.scale;
        if (b.code == 1) return s_MouseDelta.y * b.scale;
        return s_Scroll * b.scale;
    }
    return 0.0f;
}

float Input::GetAxis(const std::string& name) {
    if (!s_GameEnabled || s_Rebind.active) return 0.0f;
    auto* a = s_Map.FindAxis(name);
    if (!a) return 0.0f;
    float sum = 0.0f;
    bool unbounded = false;
    for (auto& b : a->bindings) {
        sum += BindingValue(b, a->deadzone);
        if (b.type == BindingType::MouseAxis) unbounded = true;
    }
    return unbounded ? sum : glm::clamp(sum, -1.0f, 1.0f);
}

bool Input::LoadMap(const std::string& path) {
    auto text = FileSystem::ReadText(Project::Resolve(path));
    if (!text) return false;
    try {
        s_Map.FromJson(json::parse(*text));
        return true;
    } catch (const std::exception& e) {
        ND_ERROR("Input map '{}' is invalid: {}", path, e.what());
        return false;
    }
}

bool Input::SaveMap(const std::string& path) {
    return FileSystem::WriteText(Project::Resolve(path), s_Map.ToJson().dump(2));
}

std::string Input::KeyName(int key) {
    switch (key) {
    case Key::Space: return "Space"; case Key::Escape: return "Escape"; case Key::Enter: return "Enter";
    case Key::Tab: return "Tab"; case Key::Backspace: return "Backspace"; case Key::Delete: return "Delete";
    case Key::Right: return "Right"; case Key::Left: return "Left"; case Key::Down: return "Down"; case Key::Up: return "Up";
    case Key::LeftShift: return "Left Shift"; case Key::RightShift: return "Right Shift";
    case Key::LeftControl: return "Left Ctrl"; case Key::RightControl: return "Right Ctrl";
    case Key::LeftAlt: return "Left Alt"; case Key::RightAlt: return "Right Alt";
    case Key::CapsLock: return "Caps Lock"; case Key::Home: return "Home"; case Key::End: return "End";
    case Key::PageUp: return "Page Up"; case Key::PageDown: return "Page Down"; case Key::Insert: return "Insert";
    default: break;
    }
    if (key >= Key::F1 && key <= Key::F12) return "F" + std::to_string(key - Key::F1 + 1);
    if (key >= Key::KP0 && key <= Key::KP9) return "Num " + std::to_string(key - Key::KP0);
    if ((key >= Key::A && key <= Key::Z) || (key >= Key::D0 && key <= Key::D9)) return std::string(1, (char)key);
    const char* n = glfwGetKeyName(key, 0);
    if (n) return n;
    return "Key " + std::to_string(key);
}

std::string Input::BindingName(const InputBinding& b) {
    static const char* padButtons[] = { "Pad A", "Pad B", "Pad X", "Pad Y", "Pad LB", "Pad RB", "Pad Back", "Pad Start", "Pad Guide",
                                        "Pad L3", "Pad R3", "Pad Up", "Pad Right", "Pad Down", "Pad Left" };
    static const char* padAxes[] = { "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "LT", "RT" };
    static const char* mouseAxes[] = { "Mouse X", "Mouse Y", "Scroll" };
    std::string sign = b.scale < 0 ? " (-)" : "";
    switch (b.type) {
    case BindingType::Key: return KeyName(b.code) + sign;
    case BindingType::MouseButton: return "Mouse " + std::to_string(b.code + 1) + sign;
    case BindingType::GamepadButton: return (b.code >= 0 && b.code < Gamepad::ButtonCount ? padButtons[b.code] : "Pad ?") + sign;
    case BindingType::GamepadAxis: return (b.code >= 0 && b.code < Gamepad::AxisCount ? padAxes[b.code] : "Axis ?") + sign;
    case BindingType::MouseAxis: return (b.code >= 0 && b.code < 3 ? mouseAxes[b.code] : "Mouse ?") + sign;
    }
    return "?";
}

// ---------------------------------------------------------------- InputMap
InputAction* InputMap::FindAction(const std::string& name) {
    for (auto& a : actions) if (a.name == name) return &a;
    return nullptr;
}
InputAxis* InputMap::FindAxis(const std::string& name) {
    for (auto& a : axes) if (a.name == name) return &a;
    return nullptr;
}

void InputMap::SetDefaults() {
    using BT = BindingType;
    actions = {
        { "Jump",     { { BT::Key, Key::Space }, { BT::GamepadButton, Gamepad::A } } },
        { "Sprint",   { { BT::Key, Key::LeftShift }, { BT::GamepadButton, Gamepad::LeftThumb } } },
        { "Fire",     { { BT::MouseButton, Mouse::Left }, { BT::GamepadAxis, Gamepad::RightTrigger } } },
        { "Interact", { { BT::Key, Key::E }, { BT::GamepadButton, Gamepad::X } } },
        { "Pause",    { { BT::Key, Key::Escape }, { BT::GamepadButton, Gamepad::Start } } },
    };
    axes = {
        { "MoveX", { { BT::Key, Key::D, 1 }, { BT::Key, Key::A, -1 }, { BT::Key, Key::Right, 1 }, { BT::Key, Key::Left, -1 },
                     { BT::GamepadAxis, Gamepad::LeftX, 1 } } },
        { "MoveY", { { BT::Key, Key::W, 1 }, { BT::Key, Key::S, -1 }, { BT::Key, Key::Up, 1 }, { BT::Key, Key::Down, -1 },
                     { BT::GamepadAxis, Gamepad::LeftY, -1 } } },
        { "LookX", { { BT::MouseAxis, 0, 0.1f }, { BT::GamepadAxis, Gamepad::RightX, 2.5f } } },
        { "LookY", { { BT::MouseAxis, 1, 0.1f }, { BT::GamepadAxis, Gamepad::RightY, 2.5f } } },
        { "Zoom",  { { BT::MouseAxis, 2, 1.0f } } },
    };
}

static json BindingToJson(const InputBinding& b) {
    static const char* types[] = { "key", "mouseButton", "gamepadButton", "gamepadAxis", "mouseAxis" };
    return { { "type", types[(int)b.type] }, { "code", b.code }, { "scale", b.scale } };
}
static InputBinding BindingFromJson(const json& j) {
    InputBinding b;
    std::string t = JGet<std::string>(j, "type", "key");
    if (t == "mouseButton") b.type = BindingType::MouseButton;
    else if (t == "gamepadButton") b.type = BindingType::GamepadButton;
    else if (t == "gamepadAxis") b.type = BindingType::GamepadAxis;
    else if (t == "mouseAxis") b.type = BindingType::MouseAxis;
    b.code = JGet(j, "code", 0);
    b.scale = JGet(j, "scale", 1.0f);
    return b;
}

json InputMap::ToJson() const {
    json j;
    j["actions"] = json::array();
    for (auto& a : actions) {
        json ja = { { "name", a.name }, { "bindings", json::array() } };
        for (auto& b : a.bindings) ja["bindings"].push_back(BindingToJson(b));
        j["actions"].push_back(ja);
    }
    j["axes"] = json::array();
    for (auto& a : axes) {
        json ja = { { "name", a.name }, { "deadzone", a.deadzone }, { "bindings", json::array() } };
        for (auto& b : a.bindings) ja["bindings"].push_back(BindingToJson(b));
        j["axes"].push_back(ja);
    }
    return j;
}

void InputMap::FromJson(const json& j) {
    actions.clear();
    axes.clear();
    for (auto& ja : j.value("actions", json::array())) {
        InputAction a;
        a.name = JGet<std::string>(ja, "name", "");
        for (auto& jb : ja.value("bindings", json::array())) a.bindings.push_back(BindingFromJson(jb));
        actions.push_back(a);
    }
    for (auto& ja : j.value("axes", json::array())) {
        InputAxis a;
        a.name = JGet<std::string>(ja, "name", "");
        a.deadzone = JGet(ja, "deadzone", 0.15f);
        for (auto& jb : ja.value("bindings", json::array())) a.bindings.push_back(BindingFromJson(jb));
        axes.push_back(a);
    }
}

} // namespace nd
