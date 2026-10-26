#include "nd/particles/Particles.h"
#include "nd/renderer/GL.h"
#include <cmath>

namespace nd {

ParticleRuntime::ParticleRuntime() {
    // Each emitter owns its quad so its VAO can carry its own instance buffer.
    std::vector<Vertex> v(4);
    v[0].position = { -0.5f, -0.5f, 0 }; v[0].uv = { 0, 0 };
    v[1].position = { 0.5f, -0.5f, 0 };  v[1].uv = { 1, 0 };
    v[2].position = { 0.5f, 0.5f, 0 };   v[2].uv = { 1, 1 };
    v[3].position = { -0.5f, 0.5f, 0 };  v[3].uv = { 0, 1 };
    quad = MakeRef<Mesh>(v, std::vector<uint32_t>{ 0, 1, 2, 0, 2, 3 });
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(mat4) * 64, nullptr, GL_STREAM_DRAW);
    quad->AttachInstanceBuffer(vbo);
    m_Seed = (uint32_t)(uintptr_t)this;
}

ParticleRuntime::~ParticleRuntime() { if (vbo && g_GLContextAlive) glDeleteBuffers(1, &vbo); }

float ParticleRuntime::Rand() {
    m_Seed = m_Seed * 1664525u + 1013904223u;
    return (m_Seed >> 8) / 16777216.0f;
}

void ParticleRuntime::Emit(ParticleEmitterComponent& e, const mat4& world, int count) {
    for (int i = 0; i < count && (int)particles.size() < e.maxParticles; i++) {
        Particle p;
        vec3 local(0.0f);
        if (e.shape == ParticleShape::Sphere) {
            vec3 d(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1);
            if (glm::length(d) > 1e-4f) d = glm::normalize(d);
            local = d * e.shapeSize.x * std::cbrt(Rand());
        } else if (e.shape == ParticleShape::Box) {
            local = vec3(Rand() - 0.5f, Rand() - 0.5f, Rand() - 0.5f) * e.shapeSize;
        }
        vec3 vel = e.velocity + vec3(Rand() * 2 - 1, Rand() * 2 - 1, Rand() * 2 - 1) * e.spread;
        if (e.worldSpace) {
            p.position = vec3(world * vec4(local, 1.0f));
            p.velocity = mat3(world) * vel;
        } else {
            p.position = local;
            p.velocity = vel;
        }
        p.age = 0.0f;
        p.life = std::max(0.05f, e.lifetime + (Rand() * 2 - 1) * e.lifetimeVariance);
        p.rotation = Rand() * 2.0f * PI;
        p.spin = (Rand() * 2 - 1) * 2.0f;
        particles.push_back(p);
    }
}

void ParticleRuntime::Update(ParticleEmitterComponent& e, const mat4& world, float dt) {
    if (!m_Started) {
        m_Started = true;
        if (e.burst > 0 && e.playing) Emit(e, world, e.burst);
    }
    if (e.playing) {
        m_Accum += e.rate * dt;
        int n = (int)m_Accum;
        m_Accum -= n;
        if (n > 0) Emit(e, world, n);
        if (!e.loop && e.rate <= 0.0f && e.burst > 0) e.playing = false;
    }
    float dragFactor = 1.0f / (1.0f + e.drag * dt);
    for (size_t i = 0; i < particles.size();) {
        Particle& p = particles[i];
        p.age += dt;
        if (p.age >= p.life) {
            p = particles.back();
            particles.pop_back();
            continue;
        }
        p.velocity += e.gravity * dt;
        p.velocity *= dragFactor;
        p.position += p.velocity * dt;
        p.rotation += p.spin * dt;
        i++;
    }
}

int ParticleRuntime::PrepareDraw(const ParticleEmitterComponent& e, const mat4& world, const vec3& cam) {
    if (!e.additive) {
        std::sort(particles.begin(), particles.end(), [&](const Particle& a, const Particle& b) {
            return glm::distance(a.position, cam) > glm::distance(b.position, cam);
        });
    }
    m_Instances.resize(particles.size());
    for (size_t i = 0; i < particles.size(); i++) {
        const Particle& p = particles[i];
        float t = p.age / p.life;
        vec3 pos = e.worldSpace ? p.position : vec3(world * vec4(p.position, 1.0f));
        mat4 m(0.0f);
        m[0] = vec4(pos, glm::mix(e.startSize, e.endSize, t));
        m[1] = glm::mix(e.startColor, e.endColor, t);
        m[2] = vec4(p.rotation, 0, 0, 0);
        m_Instances[i] = m;
    }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, m_Instances.size() * sizeof(mat4), m_Instances.data(), GL_STREAM_DRAW);
    return (int)m_Instances.size();
}

namespace ParticlePresets {
void Fire(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 60; e.lifetime = 0.9f; e.lifetimeVariance = 0.3f; e.shape = ParticleShape::Sphere; e.shapeSize = vec3(0.25f);
    e.velocity = { 0, 1.8f, 0 }; e.spread = 0.35f; e.startColor = { 1.0f, 0.75f, 0.25f, 1.0f }; e.endColor = { 0.9f, 0.15f, 0.02f, 0.0f };
    e.startSize = 0.55f; e.endSize = 0.1f; e.additive = true;
}
void Smoke(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 12; e.lifetime = 3.5f; e.lifetimeVariance = 0.8f; e.shape = ParticleShape::Sphere; e.shapeSize = vec3(0.3f);
    e.velocity = { 0.3f, 1.2f, 0 }; e.spread = 0.25f; e.drag = 0.3f; e.startColor = { 0.35f, 0.35f, 0.35f, 0.45f }; e.endColor = { 0.6f, 0.6f, 0.62f, 0.0f };
    e.startSize = 0.5f; e.endSize = 2.2f; e.additive = false;
}
void Sparks(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 15; e.lifetime = 1.2f; e.lifetimeVariance = 0.4f; e.shape = ParticleShape::Sphere; e.shapeSize = vec3(0.15f);
    e.velocity = { 0, 3.5f, 0 }; e.spread = 1.6f; e.gravity = { 0, -6.0f, 0 }; e.startColor = { 1.0f, 0.85f, 0.4f, 1.0f }; e.endColor = { 1.0f, 0.3f, 0.0f, 0.0f };
    e.startSize = 0.08f; e.endSize = 0.02f; e.additive = true;
}
void Magic(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 35; e.lifetime = 1.6f; e.shape = ParticleShape::Sphere; e.shapeSize = vec3(0.6f);
    e.velocity = { 0, 0.6f, 0 }; e.spread = 0.5f; e.startColor = { 0.4f, 0.8f, 1.0f, 1.0f }; e.endColor = { 0.8f, 0.3f, 1.0f, 0.0f };
    e.startSize = 0.18f; e.endSize = 0.02f; e.additive = true;
}
void Rain(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 600; e.maxParticles = 4000; e.lifetime = 1.2f; e.lifetimeVariance = 0.1f; e.shape = ParticleShape::Box; e.shapeSize = { 40, 0.5f, 40 };
    e.velocity = { 0.5f, -18.0f, 0 }; e.spread = 0.3f; e.startColor = { 0.7f, 0.75f, 0.85f, 0.5f }; e.endColor = { 0.7f, 0.75f, 0.85f, 0.4f };
    e.startSize = 0.06f; e.endSize = 0.06f; e.additive = false;
}
void Dust(ParticleEmitterComponent& e) {
    e = ParticleEmitterComponent{};
    e.rate = 8; e.lifetime = 6.0f; e.lifetimeVariance = 2.0f; e.shape = ParticleShape::Box; e.shapeSize = { 12, 4, 12 };
    e.velocity = { 0.1f, 0.05f, 0 }; e.spread = 0.15f; e.startColor = { 1.0f, 0.95f, 0.8f, 0.0f }; e.endColor = { 1.0f, 0.95f, 0.8f, 0.6f };
    e.startSize = 0.05f; e.endSize = 0.03f; e.additive = true;
}
} // namespace ParticlePresets

} // namespace nd
