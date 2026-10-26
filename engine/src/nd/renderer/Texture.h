#pragma once
#include "nd/core/Base.h"

namespace nd {

struct TextureSpec {
    bool linear = true;        // false = nearest (pixel art)
    bool mipmaps = true;
    bool repeat = true;
    bool srgb = false;
};

class Texture2D {
public:
    static Ref<Texture2D> FromFile(const std::string& absolutePath, const TextureSpec& spec = {});
    static Ref<Texture2D> FromMemory(const void* encoded, size_t size, const TextureSpec& spec = {});
    static Ref<Texture2D> FromPixels(int w, int h, int channels, const void* pixels, const TextureSpec& spec = {});
    static Ref<Texture2D> White();
    static Ref<Texture2D> Checker();

    ~Texture2D();
    void Bind(int slot) const;
    uint32_t Id() const { return m_Id; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    const std::string& Path() const { return m_Path; }

    void SetPixels(const void* rgba);   // replace contents (same size, RGBA8)

private:
    uint32_t m_Id = 0;
    int m_Width = 0, m_Height = 0;
    std::string m_Path;
    TextureSpec m_Spec;
};

} // namespace nd
