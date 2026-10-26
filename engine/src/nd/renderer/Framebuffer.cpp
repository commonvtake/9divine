#include "nd/renderer/Framebuffer.h"
#include "nd/renderer/GL.h"
#include "nd/core/Log.h"

namespace nd {

Framebuffer::Framebuffer(const FramebufferSpec& spec) : m_Spec(spec) {
    for (auto f : spec.attachments) {
        if (f == FBFormat::Depth24Stencil8 || f == FBFormat::DepthTexture) m_DepthFormat = f;
        else if (f != FBFormat::None) m_ColorFormats.push_back(f);
    }
    Invalidate();
}

Framebuffer::~Framebuffer() {
    if (!g_GLContextAlive) return;
    glDeleteFramebuffers(1, &m_Id);
    if (!m_ColorIds.empty()) glDeleteTextures((GLsizei)m_ColorIds.size(), m_ColorIds.data());
    if (m_DepthId) glDeleteTextures(1, &m_DepthId);
}

void Framebuffer::Invalidate() {
    if (m_Id) {
        glDeleteFramebuffers(1, &m_Id);
        if (!m_ColorIds.empty()) glDeleteTextures((GLsizei)m_ColorIds.size(), m_ColorIds.data());
        if (m_DepthId) glDeleteTextures(1, &m_DepthId);
        m_ColorIds.clear();
        m_DepthId = 0;
    }
    int w = std::max(1, m_Spec.width), h = std::max(1, m_Spec.height);
    glGenFramebuffers(1, &m_Id);
    glBindFramebuffer(GL_FRAMEBUFFER, m_Id);

    for (size_t i = 0; i < m_ColorFormats.size(); i++) {
        uint32_t tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        switch (m_ColorFormats[i]) {
        case FBFormat::RGBA8: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); break;
        case FBFormat::RGBA16F: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr); break;
        case FBFormat::R32I: glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, w, h, 0, GL_RED_INTEGER, GL_INT, nullptr); break;
        default: break;
        }
        bool integer = m_ColorFormats[i] == FBFormat::R32I;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, integer ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, integer ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D, tex, 0);
        m_ColorIds.push_back(tex);
    }

    if (m_DepthFormat != FBFormat::None) {
        glGenTextures(1, &m_DepthId);
        glBindTexture(GL_TEXTURE_2D, m_DepthId);
        if (m_DepthFormat == FBFormat::Depth24Stencil8) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthId, 0);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border[] = { 1, 1, 1, 1 };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            // Hardware PCF
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_DepthId, 0);
        }
    }

    if (m_ColorIds.empty()) {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    } else {
        std::vector<GLenum> bufs;
        for (size_t i = 0; i < m_ColorIds.size(); i++) bufs.push_back(GL_COLOR_ATTACHMENT0 + (GLenum)i);
        glDrawBuffers((GLsizei)bufs.size(), bufs.data());
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        ND_ERROR("Framebuffer is incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_Id);
    glViewport(0, 0, m_Spec.width, m_Spec.height);
}

void Framebuffer::BindDefault(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}

void Framebuffer::Resize(int width, int height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return;
    if (width == m_Spec.width && height == m_Spec.height) return;
    m_Spec.width = width;
    m_Spec.height = height;
    Invalidate();
}

int Framebuffer::ReadPixelInt(int attachment, int x, int y) const {
    if (x < 0 || y < 0 || x >= m_Spec.width || y >= m_Spec.height) return -1;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Id);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment);
    int value = -1;
    glReadPixels(x, y, 1, 1, GL_RED_INTEGER, GL_INT, &value);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return value;
}

void Framebuffer::ClearAttachmentInt(int attachment, int value) const {
    glClearBufferiv(GL_COLOR, attachment, &value);
}

void Framebuffer::BlitToScreen(int sw, int sh) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_Id);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glBlitFramebuffer(0, 0, m_Spec.width, m_Spec.height, 0, 0, sw, sh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace nd
