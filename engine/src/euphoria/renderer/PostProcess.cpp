#include "euphoria/renderer/PostProcess.h"
#include "euphoria/renderer/Renderer.h"
#include "euphoria/renderer/Shader.h"
#include "euphoria/renderer/GL.h"
#include "euphoria/scene/Scene.h"
#include "euphoria/core/Profiler.h"
#include <cmath>

namespace eu {

extern bool g_GLContextAlive;

// ------------------------------------------------------------------ RenderTarget
void RenderTarget::Resize(int w, int h, uint32_t fmt, bool linearFilter) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (fbo && w == width && h == height && fmt == internalFormat) return;
    Release();
    width = w;
    height = h;
    internalFormat = fmt;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    GLenum type = (fmt == GL_RGBA8 || fmt == GL_R8) ? GL_UNSIGNED_BYTE : GL_FLOAT;
    GLenum format = (fmt == GL_R8 || fmt == GL_R16F) ? GL_RED : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::Bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void RenderTarget::Release() {
    if (fbo && g_GLContextAlive) {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &color);
    }
    fbo = color = 0;
    width = height = 0;
}

// ------------------------------------------------------------------ PostProcess
namespace {
struct PostData {
    uint32_t vao = 0;
    RenderTarget ao, aoBlur, ldr;
    static constexpr int kBloomMips = 6;
    RenderTarget bloom[kBloomMips];
    int bloomCount = 0;
    bool aoValid = false, bloomValid = false;
};
PostData* s_Post = nullptr;

void Fullscreen() {
    glBindVertexArray(s_Post->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}
void BindTex(int unit, uint32_t tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

// Per-channel gains for a white-balance shift. Temperature moves along blue <-> orange,
// tint along green <-> magenta; the result is normalized to keep brightness.
vec3 WhiteBalance(float temperature, float tint) {
    vec3 g(1.0f + 0.25f * temperature, 1.0f - 0.18f * tint, 1.0f - 0.25f * temperature);
    float lum = glm::dot(g, vec3(0.2126f, 0.7152f, 0.0722f));
    return g / std::max(lum, 1e-3f);
}
} // namespace

void PostProcess::Init() {
    s_Post = new PostData();
    glGenVertexArrays(1, &s_Post->vao);
}

void PostProcess::Shutdown() {
    if (!s_Post) return;
    s_Post->ao.Release();
    s_Post->aoBlur.Release();
    s_Post->ldr.Release();
    for (auto& b : s_Post->bloom) b.Release();
    if (g_GLContextAlive) glDeleteVertexArrays(1, &s_Post->vao);
    delete s_Post;
    s_Post = nullptr;
}

uint32_t PostProcess::AOTexture() { return s_Post && s_Post->aoValid ? s_Post->aoBlur.color : 0; }
uint32_t PostProcess::BloomTexture() { return s_Post && s_Post->bloomValid ? s_Post->bloom[0].color : 0; }

void PostProcess::Run(const Inputs& in, const SceneEnvironment& env, const RenderSettings& rs) {
    EU_PROFILE_SCOPE("Post");
    auto& P = *s_Post;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // 1. SSAO (half resolution)
    P.aoValid = rs.ssao && env.ssaoStrength > 0.0f && in.depth;
    if (P.aoValid) {
        int w = std::max(1, in.width / 2), h = std::max(1, in.height / 2);
        P.ao.Resize(w, h, GL_R8);
        P.aoBlur.Resize(w, h, GL_R8);
        auto ssao = Renderer::GetShader("post_ssao.glsl");
        P.ao.Bind();
        ssao->Bind();
        BindTex(0, in.depth);
        ssao->SetInt("uDepth", 0);
        ssao->SetMat4("uProj", in.projection);
        ssao->SetMat4("uInvProj", glm::inverse(in.projection));
        ssao->SetFloat("uRadius", env.ssaoRadius);
        Fullscreen();
        auto blur = Renderer::GetShader("post_ao_blur.glsl");
        P.aoBlur.Bind();
        blur->Bind();
        BindTex(0, P.ao.color);
        BindTex(1, in.depth);
        blur->SetInt("uAO", 0);
        blur->SetInt("uDepth", 1);
        blur->SetVec2("uTexel", vec2(1.0f / w, 1.0f / h));
        blur->SetFloat("uNear", in.nearClip);
        blur->SetFloat("uFar", in.farClip);
        Fullscreen();
    }

    // 2. Bloom
    P.bloomValid = rs.bloom && env.bloomIntensity > 0.0f;
    if (P.bloomValid) {
        int w = in.width, h = in.height;
        P.bloomCount = 0;
        for (int i = 0; i < PostData::kBloomMips; i++) {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            P.bloom[i].Resize(w, h, GL_RGBA16F);
            P.bloomCount++;
            if (w <= 8 || h <= 8) break;
        }
        auto down = Renderer::GetShader("post_bloom_down.glsl");
        down->Bind();
        down->SetInt("uSource", 0);
        down->SetFloat("uThreshold", env.bloomThreshold);
        down->SetFloat("uExposure", env.exposure);
        uint32_t src = in.hdrColor;
        int sw = in.width, sh = in.height;
        for (int i = 0; i < P.bloomCount; i++) {
            P.bloom[i].Bind();
            BindTex(0, src);
            down->SetInt("uFirst", i == 0 ? 1 : 0);
            down->SetVec2("uTexel", vec2(1.0f / sw, 1.0f / sh));
            Fullscreen();
            src = P.bloom[i].color;
            sw = P.bloom[i].width;
            sh = P.bloom[i].height;
        }
        auto up = Renderer::GetShader("post_bloom_up.glsl");
        up->Bind();
        up->SetInt("uSource", 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = P.bloomCount - 1; i > 0; i--) {
            P.bloom[i - 1].Bind();
            BindTex(0, P.bloom[i].color);
            up->SetVec2("uTexel", vec2(1.0f / P.bloom[i].width, 1.0f / P.bloom[i].height));
            Fullscreen();
        }
        glDisable(GL_BLEND);
    }

    // 3. Composite (to an LDR buffer if FXAA follows, else straight to the destination)
    auto comp = Renderer::GetShader("post_composite.glsl");
    if (rs.fxaa) {
        P.ldr.Resize(in.width, in.height, GL_RGBA8);
        P.ldr.Bind();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, in.destFbo);
        glViewport(0, 0, in.width, in.height);
    }
    comp->Bind();
    BindTex(0, in.hdrColor);
    BindTex(1, P.aoValid ? P.aoBlur.color : 0);
    BindTex(2, P.bloomValid ? P.bloom[0].color : 0);
    comp->SetInt("uScene", 0);
    comp->SetInt("uAO", 1);
    comp->SetInt("uBloom", 2);
    comp->SetInt("uHasAO", P.aoValid ? 1 : 0);
    comp->SetInt("uHasBloom", P.bloomValid ? 1 : 0);
    comp->SetFloat("uAOStrength", glm::clamp(env.ssaoStrength, 0.0f, 2.0f) * 0.5f);
    comp->SetFloat("uBloomIntensity", env.bloomIntensity);
    comp->SetFloat("uExposure", env.exposure);
    comp->SetFloat("uContrast", env.contrast);
    comp->SetFloat("uSaturation", env.saturation);
    comp->SetVec3("uWhiteBalance", WhiteBalance(env.temperature, env.tint));
    comp->SetFloat("uVignette", env.vignette);
    Fullscreen();

    // 4. FXAA
    if (rs.fxaa) {
        glBindFramebuffer(GL_FRAMEBUFFER, in.destFbo);
        glViewport(0, 0, in.width, in.height);
        auto fxaa = Renderer::GetShader("post_fxaa.glsl");
        fxaa->Bind();
        BindTex(0, P.ldr.color);
        fxaa->SetInt("uImage", 0);
        fxaa->SetVec2("uTexel", vec2(1.0f / in.width, 1.0f / in.height));
        Fullscreen();
    }

    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}

} // namespace eu
