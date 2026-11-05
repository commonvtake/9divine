#pragma once
// Sprite animation and sprite-sheet helpers (see SpriteAnimatorComponent).
#include "euphoria/scene/Components.h"

namespace eu::Sprite2D {

// Advances the animator and points the sprite at the current frame.
void Update(SpriteAnimatorComponent& animator, SpriteRendererComponent& sprite, float dt);

// The frame grid of an animation's sheet: frame size, columns and the frames it uses.
struct FrameGrid { ivec2 frameSize{ 0 }; int columns = 1, first = 0, count = 1; };
bool Grid(const SpriteAnimation& anim, const std::string& fallbackTexture, FrameGrid& out);
// Pixel rectangle (x, y, w, h from the top-left) of frame `frame` of the animation.
vec4 FrameRect(const FrameGrid& grid, int frame);

// An animation playing every frame of a sheet (frame size detected). `name` empty = the file name.
SpriteAnimation FromSheet(const std::string& texture, const std::string& name = "", float fps = 10.0f);
// One animation per row of a sheet (e.g. a character with a row per direction):
// names[i] for row i (rows without a name are skipped).
std::vector<SpriteAnimation> FromSheetRows(const std::string& texture, const std::vector<std::string>& names, float fps = 10.0f);

} // namespace eu::Sprite2D
