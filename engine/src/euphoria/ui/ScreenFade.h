#pragma once
#include "euphoria/core/Base.h"
#include <functional>

namespace eu {

// Full-screen fades drawn over everything (including the HUD), independent of scenes: a fade
// started in one scene finishes in the next, which is what makes door and level transitions
// seamless. Runs on real time, so it works while the game is paused.
//
//   ScreenFade::FadeOut(0.5f, [] { SceneManager::LoadScene("Scenes/Next.scene"); });
//   ScreenFade::FadeIn(0.5f);
//
//   // The classic survival-horror door: fade to black, do the work while it's dark, hold the
//   // black for a beat (the door sound plays), then fade back in.
//   ScreenFade::Transition(0.45f, 1.1f, 0.6f, [] { /* load room, move player */ });
class ScreenFade {
public:
    static void FadeOut(float seconds, std::function<void()> whenBlack = nullptr);
    static void FadeIn(float seconds);
    static void Transition(float fadeOut, float hold, float fadeIn, std::function<void()> atBlack);
    static void SetColor(const vec3& color) { s_Color = color; }

    static float Alpha() { return s_Alpha; }      // 0 = clear, 1 = fully covered
    static bool Busy() { return s_Phase != Phase::Idle || s_Alpha > 0.0f; }   // gameplay should ignore input
    static void Reset();                           // clear instantly (e.g. when play mode stops)

    // Called by the engine once per frame inside the game-UI pass (Update + Draw).
    static void UpdateAndDraw(float unscaledDt);
    static void Update(float unscaledDt);   // advance the fade; runs the black-screen callback
    static void Draw();                     // the overlay itself (needs an active game-UI frame)

private:
    enum class Phase { Idle, Out, Hold, In };
    static Phase s_Phase;
    static float s_Alpha, s_Speed, s_Hold, s_InDuration;
    static vec3 s_Color;
    static std::function<void()> s_AtBlack;
};

} // namespace eu
