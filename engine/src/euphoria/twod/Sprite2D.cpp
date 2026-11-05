#include "euphoria/twod/Sprite2D.h"
#include "euphoria/twod/Tilemap.h"
#include "euphoria/assets/AssetManager.h"
#include "euphoria/renderer/Texture.h"
#include "euphoria/core/FileSystem.h"

namespace eu::Sprite2D {

bool Grid(const SpriteAnimation& a, const std::string& fallbackTexture, FrameGrid& g) {
    const std::string& tex = a.texture.empty() ? fallbackTexture : a.texture;
    if (tex.empty()) return false;
    auto t = AssetManager::GetTexture(tex, true);
    if (!t || t->Width() <= 0) return false;
    ivec2 fs(a.frameWidth, a.frameHeight);
    if (fs.x <= 0 || fs.y <= 0) {
        ivec2 d = SpriteSheet::DetectFrameSize(tex);
        if (fs.x <= 0) fs.x = d.x > 0 ? d.x : t->Width();
        if (fs.y <= 0) fs.y = d.y > 0 ? d.y : t->Height();
    }
    fs = glm::max(fs, ivec2(1));
    g.frameSize = fs;
    g.columns = std::max(1, t->Width() / fs.x);
    int rows = std::max(1, t->Height() / fs.y), total = g.columns * rows;
    g.first = glm::clamp(a.firstFrame, 0, total - 1);
    g.count = a.frameCount > 0 ? std::min(a.frameCount, total - g.first) : total - g.first;
    g.count = std::max(1, g.count);
    return true;
}

vec4 FrameRect(const FrameGrid& g, int frame) {
    int idx = g.first + glm::clamp(frame, 0, g.count - 1);
    return vec4((float)(idx % g.columns * g.frameSize.x), (float)(idx / g.columns * g.frameSize.y), (float)g.frameSize.x, (float)g.frameSize.y);
}

void Update(SpriteAnimatorComponent& an, SpriteRendererComponent& sr, float dt) {
    if (an.animations.empty()) return;
    SpriteAnimation* a = an.current.empty() ? nullptr : an.Find(an.current);
    if (!a) {
        a = an.defaultAnimation.empty() ? nullptr : an.Find(an.defaultAnimation);
        if (!a) a = &an.animations[0];
        an.current = a->name;
        an.time = 0.0f;
        an.finished = false;
    }
    FrameGrid g;
    if (!Grid(*a, sr.texture, g)) return;
    an.time += dt * an.speed;
    float f = std::max(0.0f, an.time * a->fps);
    if (a->loop) {
        an.frame = (int)std::floor(f) % g.count;
        an.finished = false;
    } else {
        an.frame = std::min((int)f, g.count - 1);
        an.finished = f >= (float)g.count;
    }
    if (!a->texture.empty()) sr.texture = a->texture;
    sr.region = FrameRect(g, an.frame);
}

SpriteAnimation FromSheet(const std::string& texture, const std::string& name, float fps) {
    SpriteAnimation a;
    a.name = name.empty() ? fs::u8path(texture).stem().u8string() : name;
    a.texture = texture;
    a.fps = fps;
    ivec2 d = SpriteSheet::DetectFrameSize(texture);
    a.frameWidth = d.x;
    a.frameHeight = d.y;
    return a;
}

std::vector<SpriteAnimation> FromSheetRows(const std::string& texture, const std::vector<std::string>& names, float fps) {
    std::vector<SpriteAnimation> out;
    SpriteAnimation base = FromSheet(texture, "", fps);
    auto t = AssetManager::GetTexture(texture, true);
    if (!t || base.frameWidth <= 0) return out;
    int cols = std::max(1, t->Width() / base.frameWidth);
    for (size_t r = 0; r < names.size(); r++) {
        if (names[r].empty()) continue;
        SpriteAnimation a = base;
        a.name = names[r];
        a.firstFrame = (int)r * cols;
        a.frameCount = cols;
        out.push_back(a);
    }
    return out;
}

} // namespace eu::Sprite2D
