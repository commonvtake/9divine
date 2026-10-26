#pragma once
#include "nd/core/Base.h"
#include "nd/core/Json.h"

struct GLFWwindow;

namespace nd {

// Key codes match GLFW so they can be stored directly in input maps.
namespace Key {
enum : int {
    Unknown = -1, Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    Semicolon = 59, Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
    Escape = 256, Enter, Tab, Backspace, Insert, Delete, Right, Left, Down, Up, PageUp, PageDown, Home, End,
    CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    KP0 = 320, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
    LeftShift = 340, LeftControl, LeftAlt, LeftSuper, RightShift, RightControl, RightAlt, RightSuper, Menu,
    Last = 348
};
}

namespace Mouse { enum : int { Left = 0, Right = 1, Middle = 2, Button4 = 3, Button5 = 4, Last = 7 }; }

namespace Gamepad {
enum Button : int { A = 0, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb, DpadUp, DpadRight, DpadDown, DpadLeft, ButtonCount };
enum Axis : int { LeftX = 0, LeftY, RightX, RightY, LeftTrigger, RightTrigger, AxisCount };
}

enum class CursorMode { Normal, Hidden, Locked };

enum class BindingType { Key, MouseButton, GamepadButton, GamepadAxis, MouseAxis };

struct InputBinding {
    BindingType type = BindingType::Key;
    int code = 0;          // key / button / axis index (MouseAxis: 0 = X, 1 = Y, 2 = scroll)
    float scale = 1.0f;    // contribution to an axis (use -1 for the "negative" key)
};

struct InputAction {
    std::string name;
    std::vector<InputBinding> bindings;
};

struct InputAxis {
    std::string name;
    std::vector<InputBinding> bindings;
    float deadzone = 0.15f;
};

// Named actions/axes so gameplay code never hard-codes keys and players can rebind.
class InputMap {
public:
    std::vector<InputAction> actions;
    std::vector<InputAxis> axes;

    InputAction* FindAction(const std::string& name);
    InputAxis* FindAxis(const std::string& name);
    void SetDefaults();
    json ToJson() const;
    void FromJson(const json& j);
};

class Input {
public:
    static void Init(GLFWwindow* window);
    static void BeginFrame();   // call before polling events
    static void EndFrame();     // after polling: updates gamepads / rebinding

    // When false (e.g. the editor viewport isn't focused) all game queries return "nothing pressed".
    static void SetGameInputEnabled(bool enabled) { s_GameEnabled = enabled; }
    static bool IsGameInputEnabled() { return s_GameEnabled; }

    // Keyboard / mouse
    static bool IsKeyDown(int key);
    static bool IsKeyPressed(int key);    // went down this frame
    static bool IsKeyReleased(int key);
    static bool IsMouseDown(int button);
    static bool IsMousePressed(int button);
    static bool IsMouseReleased(int button);
    static vec2 MousePosition();          // window pixels
    static vec2 MouseDelta();
    static float ScrollDelta();
    static void SetCursorMode(CursorMode mode);
    static CursorMode GetCursorMode() { return s_CursorMode; }

    // Raw versions ignore the game-input gate (used by editor tools).
    static bool RawKeyDown(int key);
    static bool RawKeyPressed(int key);
    static bool RawMouseDown(int button);
    static vec2 RawMouseDelta() { return s_MouseDelta; }
    static float RawScroll() { return s_Scroll; }

    // Gamepads (0..3)
    static bool IsGamepadConnected(int pad = 0);
    static bool IsGamepadButtonDown(int button, int pad = 0);
    static bool IsGamepadButtonPressed(int button, int pad = 0);
    static float GetGamepadAxis(int axis, int pad = 0);  // deadzone applied
    static std::string GamepadName(int pad = 0);

    // Action mapping
    static InputMap& Map() { return s_Map; }
    static bool GetAction(const std::string& name);          // held
    static bool GetActionPressed(const std::string& name);   // this frame
    static bool GetActionReleased(const std::string& name);
    static float GetAxis(const std::string& name);
    static bool LoadMap(const std::string& path);
    static bool SaveMap(const std::string& path);

    // Rebinding: the next key/button/stick movement replaces binding `index`
    // (index == -1 appends a new binding). Escape cancels.
    static void BeginRebind(const std::string& name, bool isAxis, int index, float scale = 1.0f);
    static bool IsRebinding() { return s_Rebind.active; }
    static void CancelRebind() { s_Rebind.active = false; }

    static std::string BindingName(const InputBinding& b);
    static std::string KeyName(int key);

private:
    static bool BindingDown(const InputBinding& b, const bool* keys, const bool* mouse, const bool* pads);
    static float BindingValue(const InputBinding& b, float deadzone);
    static void FinishRebind(const InputBinding& b);

    static GLFWwindow* s_Window;
    static bool s_GameEnabled;
    static bool s_Keys[Key::Last + 1], s_PrevKeys[Key::Last + 1];
    static bool s_Mouse[Mouse::Last + 1], s_PrevMouse[Mouse::Last + 1];
    static bool s_Pad[4][Gamepad::ButtonCount], s_PrevPad[4][Gamepad::ButtonCount];
    static float s_PadAxes[4][Gamepad::AxisCount];
    static bool s_PadConnected[4];
    static vec2 s_MousePos, s_MouseDelta, s_LastMousePos;
    static float s_Scroll;
    static bool s_FirstMouse;
    static CursorMode s_CursorMode;
    static InputMap s_Map;

    struct RebindState { bool active = false; std::string name; bool isAxis = false; int index = -1; float scale = 1.0f; };
    static RebindState s_Rebind;

    friend struct InputCallbacks;
};

} // namespace nd
