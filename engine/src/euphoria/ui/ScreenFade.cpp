#include "euphoria/ui/ScreenFade.h"
#include "euphoria/ui/GameUI.h"
#include <algorithm>

namespace eu {

ScreenFade::Phase ScreenFade::s_Phase = ScreenFade::Phase::Idle;
float ScreenFade::s_Alpha = 0.0f;
float ScreenFade::s_Speed = 2.0f;
float ScreenFade::s_Hold = 0.0f;
float ScreenFade::s_InDuration = 0.5f;
vec3 ScreenFade::s_Color{ 0.0f };
std::function<void()> ScreenFade::s_AtBlack;

void ScreenFade::FadeOut(float seconds, std::function<void()> whenBlack) {
    s_Phase = Phase::Out;
    s_Speed = 1.0f / std::max(seconds, 1e-3f);
    s_AtBlack = std::move(whenBlack);
    s_Hold = 0.0f;
    s_InDuration = -1.0f;     // stay black until FadeIn
}

void ScreenFade::FadeIn(float seconds) {
    s_Phase = Phase::In;
    s_Speed = 1.0f / std::max(seconds, 1e-3f);
}

void ScreenFade::Transition(float fadeOut, float hold, float fadeIn, std::function<void()> atBlack) {
    FadeOut(fadeOut, std::move(atBlack));
    s_Hold = std::max(hold, 0.0f);
    s_InDuration = std::max(fadeIn, 1e-3f);
}

void ScreenFade::Reset() {
    s_Phase = Phase::Idle;
    s_Alpha = 0.0f;
    s_AtBlack = nullptr;
}

void ScreenFade::UpdateAndDraw(float dt) {
    Update(dt);
    Draw();
}

void ScreenFade::Update(float dt) {
    dt = std::min(dt, 0.05f);   // a long loading frame must not skip the hold
    switch (s_Phase) {
    case Phase::Idle: break;
    case Phase::Out:
        s_Alpha = std::min(1.0f, s_Alpha + dt * s_Speed);
        if (s_Alpha >= 1.0f) {
            auto fn = std::move(s_AtBlack);   // may start another fade; take it first
            s_AtBlack = nullptr;
            s_Phase = s_InDuration > 0.0f ? Phase::Hold : Phase::Idle;   // FadeOut alone stays black
            if (fn) fn();
        }
        break;
    case Phase::Hold:
        s_Hold -= dt;
        if (s_Hold <= 0.0f) FadeIn(s_InDuration);
        break;
    case Phase::In:
        s_Alpha = std::max(0.0f, s_Alpha - dt * s_Speed);
        if (s_Alpha <= 0.0f) s_Phase = Phase::Idle;
        break;
    }
}

void ScreenFade::Draw() {
    if (s_Alpha > 0.0f) {
        vec2 c = UI::CanvasSize();
        // Ease so the last moments before black (and the first after) feel deliberate
        float a = s_Alpha * s_Alpha * (3.0f - 2.0f * s_Alpha);
        UI::Panel({ { -10.0f, -10.0f }, c + vec2(20.0f) }, vec4(s_Color, a));
    }
}

} // namespace eu
