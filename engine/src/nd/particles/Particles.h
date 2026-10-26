#pragma once
#include "nd/core/Base.h"
#include "nd/scene/Components.h"
#include "nd/renderer/Mesh.h"

namespace nd {

// CPU-simulated particles rendered as instanced camera-facing quads.
class ParticleRuntime {
public:
    ParticleRuntime();
    ~ParticleRuntime();

    struct Particle {
        vec3 position;
        vec3 velocity;
        float age, life;
        float rotation, spin;
    };

    void Update(ParticleEmitterComponent& e, const mat4& world, float dt);
    void Emit(ParticleEmitterComponent& e, const mat4& world, int count);
    void Clear() { particles.clear(); m_Accum = 0.0f; m_Started = false; }
    // Uploads instance data (sorted back-to-front when using alpha blending).
    int PrepareDraw(const ParticleEmitterComponent& e, const mat4& world, const vec3& cameraPos);

    std::vector<Particle> particles;
    Ref<Mesh> quad;
    uint32_t vbo = 0;

private:
    std::vector<mat4> m_Instances;
    float m_Accum = 0.0f;
    bool m_Started = false;
    uint32_t m_Seed = 12345;
    float Rand();   // 0..1
};

namespace ParticlePresets {
void Fire(ParticleEmitterComponent& e);
void Smoke(ParticleEmitterComponent& e);
void Sparks(ParticleEmitterComponent& e);
void Magic(ParticleEmitterComponent& e);
void Rain(ParticleEmitterComponent& e);
void Dust(ParticleEmitterComponent& e);
}

} // namespace nd
