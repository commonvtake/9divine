#include "nd/renderer/Texture.h"
#include "nd/renderer/GL.h"
#include "nd/core/Log.h"
#include <stb_image.h>

namespace nd {

Ref<Texture2D> Texture2D::FromPixels(int w, int h, int channels, const void* pixels, const TextureSpec& spec) {
    auto tex = MakeRef<Texture2D>();
    tex->m_Width = w;
    tex->m_Height = h;
    tex->m_Spec = spec;
    GLenum fmt = channels == 4 ? GL_RGBA : channels == 3 ? GL_RGB : channels == 2 ? GL_RG : GL_RED;
    GLenum internal = channels == 4 ? (spec.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8)
                    : channels == 3 ? (spec.srgb ? GL_SRGB8 : GL_RGB8)
                    : channels == 2 ? GL_RG8 : GL_R8;
    glGenTextures(1, &tex->m_Id);
    glBindTexture(GL_TEXTURE_2D, tex->m_Id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
    if (channels == 1) {
        GLint swizzle[] = { GL_RED, GL_RED, GL_RED, GL_ONE };
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    }
    GLenum wrap = spec.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, spec.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    spec.mipmaps ? (spec.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (spec.linear ? GL_LINEAR : GL_NEAREST));
    if (spec.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return tex;
}

Ref<Texture2D> Texture2D::FromFile(const std::string& path, const TextureSpec& spec) {
    int w, h, c;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 0);
    if (!data) {
        ND_ERROR("Failed to load texture {}: {}", path, stbi_failure_reason());
        return nullptr;
    }
    auto tex = FromPixels(w, h, c, data, spec);
    tex->m_Path = path;
    stbi_image_free(data);
    return tex;
}

Ref<Texture2D> Texture2D::FromMemory(const void* encoded, size_t size, const TextureSpec& spec) {
    int w, h, c;
    // glTF images use top-left origin with uv (0,0) at top-left, so no flip here.
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load_from_memory((const stbi_uc*)encoded, (int)size, &w, &h, &c, 0);
    if (!data) {
        ND_ERROR("Failed to decode embedded texture: {}", stbi_failure_reason());
        return nullptr;
    }
    auto tex = FromPixels(w, h, c, data, spec);
    stbi_image_free(data);
    return tex;
}

Ref<Texture2D> Texture2D::White() {
    static Ref<Texture2D> white;
    if (!white) {
        uint32_t px = 0xFFFFFFFF;
        white = FromPixels(1, 1, 4, &px, { true, false, true });
    }
    return white;
}

Ref<Texture2D> Texture2D::Checker() {
    static Ref<Texture2D> checker;
    if (!checker) {
        uint32_t px[64 * 64];
        for (int y = 0; y < 64; y++)
            for (int x = 0; x < 64; x++)
                px[y * 64 + x] = ((x / 8 + y / 8) % 2) ? 0xFFDDDDDD : 0xFF999999;
        checker = FromPixels(64, 64, 4, px, { true, true, true });
    }
    return checker;
}

Texture2D::~Texture2D() { if (m_Id && g_GLContextAlive) glDeleteTextures(1, &m_Id); }

void Texture2D::Bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_Id);
}

void Texture2D::SetPixels(const void* rgba) {
    glBindTexture(GL_TEXTURE_2D, m_Id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_Width, m_Height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (m_Spec.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
}

} // namespace nd
