#pragma once
#include "nd/core/Base.h"

namespace nd {

enum class FBFormat { None, RGBA8, RGBA16F, R32I, Depth24Stencil8, DepthTexture };

struct FramebufferSpec {
    int width = 1280, height = 720;
    std::vector<FBFormat> attachments = { FBFormat::RGBA8, FBFormat::R32I, FBFormat::Depth24Stencil8 };
};

class Framebuffer {
public:
    explicit Framebuffer(const FramebufferSpec& spec);
    ~Framebuffer();

    void Bind() const;
    static void BindDefault(int width, int height);
    void Resize(int width, int height);

    int ReadPixelInt(int attachment, int x, int y) const;
    void ClearAttachmentInt(int attachment, int value) const;

    uint32_t ColorAttachment(int index = 0) const { return index < (int)m_ColorIds.size() ? m_ColorIds[index] : 0; }
    uint32_t DepthAttachment() const { return m_DepthId; }
    uint32_t Id() const { return m_Id; }
    int Width() const { return m_Spec.width; }
    int Height() const { return m_Spec.height; }
    int ColorCount() const { return (int)m_ColorIds.size(); }

    void BlitToScreen(int screenW, int screenH) const;

private:
    void Invalidate();
    FramebufferSpec m_Spec;
    uint32_t m_Id = 0;
    std::vector<uint32_t> m_ColorIds;
    std::vector<FBFormat> m_ColorFormats;
    uint32_t m_DepthId = 0;
    FBFormat m_DepthFormat = FBFormat::None;
};

} // namespace nd
