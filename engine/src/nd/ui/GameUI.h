#pragma once
// Immediate-mode in-game UI (HUDs, menus, health bars).
// Coordinates use a 1920x1080 reference canvas that scales with the window height.
//
//   void OnGUI() override {
//       UI::ProgressBar(UI::Anchored(UIAnchor::TopLeft, {30, 30}, {400, 28}), health / 100.0f);
//       if (UI::Button("Resume", UI::Anchored(UIAnchor::Center, {0, 0}, {320, 64}))) Resume();
//   }
#include "nd/core/Base.h"
#include "nd/scene/Components.h"

namespace nd {

class Texture2D;

namespace UI {

struct Rect {
    vec2 pos{ 0.0f };
    vec2 size{ 0.0f };
    bool Contains(const vec2& p) const { return p.x >= pos.x && p.y >= pos.y && p.x <= pos.x + size.x && p.y <= pos.y + size.y; }
};

enum class Align { Left, Center, Right };

struct Style {
    vec4 buttonColor{ 0.12f, 0.14f, 0.16f, 0.85f };
    vec4 buttonHover{ 0.22f, 0.50f, 0.40f, 0.95f };
    vec4 buttonPressed{ 0.16f, 0.38f, 0.30f, 1.0f };
    vec4 textColor{ 1.0f };
    vec4 barBack{ 0.0f, 0.0f, 0.0f, 0.55f };
    vec4 barFill{ 0.85f, 0.25f, 0.25f, 1.0f };
    float border = 2.0f;
    vec4 borderColor{ 1.0f, 1.0f, 1.0f, 0.15f };
};

void Init();
void Shutdown();
void BeginFrame(int pixelWidth, int pixelHeight, const vec2& mousePixels, bool mouseDown, bool mousePressed);
void EndFrame();                 // draws everything submitted this frame
void SetCamera(const mat4& viewProj);

Style& GetStyle();
vec2 CanvasSize();               // reference units (height is always 1080)
Rect Anchored(UIAnchor anchor, const vec2& offset, const vec2& size);
float MeasureText(const std::string& text, float size);

void Panel(const Rect& r, const vec4& color);
void PanelBordered(const Rect& r, const vec4& color, const vec4& border, float thickness = 2.0f);
void Text(const vec2& pos, const std::string& text, float size = 28.0f, const vec4& color = vec4(1.0f), Align align = Align::Left);
void TextInRect(const Rect& r, const std::string& text, float size = 28.0f, const vec4& color = vec4(1.0f), Align align = Align::Center);
bool Button(const std::string& label, const Rect& r, float textSize = 30.0f);
void ProgressBar(const Rect& r, float value, const vec4& fill = vec4(-1.0f), const vec4& back = vec4(-1.0f));
void Image(const Rect& r, const Ref<Texture2D>& texture, const vec4& tint = vec4(1.0f));

// World-space helpers (needs SetCamera, which Scene does automatically).
bool WorldToCanvas(const vec3& world, vec2& out);
void WorldProgressBar(const vec3& world, float value, const vec2& size = { 90, 10 }, const vec4& fill = vec4(-1.0f));
void WorldText(const vec3& world, const std::string& text, float size = 24.0f, const vec4& color = vec4(1.0f));

bool IsMouseOverUI();            // a button was hovered this frame

} // namespace UI
} // namespace nd
