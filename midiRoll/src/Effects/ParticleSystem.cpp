#include "ParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace pfd {

bool ParticleSystem::Initialize(ID3D11Device*) {
    m_particles.reserve(MAX_PARTICLES);
    return true;
}

void ParticleSystem::EmitBurst(float x, float y, util::Color color, int count) {
    std::uniform_real_distribution<float> distAngle(-1.57f * 1.2f, -1.57f * 0.4f);
    std::uniform_real_distribution<float> distSpeed(80.0f, 300.0f);
    std::uniform_real_distribution<float> distSize(1.0f, 4.0f);
    std::uniform_real_distribution<float> distGlowSize(3.0f, 8.0f);
    std::uniform_real_distribution<float> distLife(0.5f, 1.2f);

    for (int i = 0; i < count && m_particles.size() < MAX_PARTICLES; i++) {
        bool isGlow = std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.4f;
        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);
        float sz = isGlow ? distGlowSize(m_rng) : distSize(m_rng);

        m_particles.push_back({
            x + std::uniform_real_distribution<float>(-3, 3)(m_rng), y,
            std::cosf(angle) * speed, std::sinf(angle) * speed,
            color.r, color.g, color.b, color.a,
            distLife(m_rng), 1.0f, sz,
            isGlow ? 1u : 0u
        });
    }
}

void ParticleSystem::EmitContinuous(float x, float y, util::Color color) {
    if (m_particles.size() >= MAX_PARTICLES) return;
    std::uniform_real_distribution<float> distAngle(-1.8f, -1.35f);
    std::uniform_real_distribution<float> distSpeed(50.0f, 170.0f);
    std::uniform_real_distribution<float> distOffset(-6.0f, 6.0f);

    float life = m_emberMode ? 2.5f : 1.0f;
    float angle = distAngle(m_rng);
    float speed = distSpeed(m_rng);
    bool isGlow = std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.3f;

    m_particles.push_back({
        x + distOffset(m_rng), y + std::uniform_real_distribution<float>(-2, 2)(m_rng),
        std::cosf(angle) * speed, std::sinf(angle) * speed,
        color.r, color.g, color.b, color.a,
        life, life,
        isGlow ? (2.0f + std::uniform_real_distribution<float>(0, 4)(m_rng))
               : (1.0f + std::uniform_real_distribution<float>(0, 2)(m_rng)),
        isGlow ? 1u : 0u
    });
}

void ParticleSystem::EmitSparks(float x, float y, util::Color color) {
    std::uniform_real_distribution<float> distAngle(0, 6.28318f);
    std::uniform_real_distribution<float> distSpeed(180.0f, 530.0f);

    for (int i = 0; i < 12 && m_particles.size() < MAX_PARTICLES; i++) {
        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);
        m_particles.push_back({
            x, y,
            std::cosf(angle) * speed, std::sinf(angle) * speed - 60.0f,
            color.r, color.g, color.b, color.a,
            0.3f, 0.6f,
            1.0f + std::uniform_real_distribution<float>(0, 1.5f)(m_rng),
            0u
        });
    }
}

void ParticleSystem::Update(float dt) {
    float gravity = m_emberMode ? -180.0f : m_gravity;
    float time = (float)std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    for (size_t i = m_particles.size(); i > 0; --i) {
        auto& p = m_particles[i - 1];
        // Sway
        p.vx += std::sinf(time * 3.0f + p.y * 0.01f) * 3.5f * dt * 10.0f;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.vy += gravity * dt;
        // Drag
        float drag = (p.flags & 1) ? 0.97f : 0.99f;
        p.vx *= drag;
        p.vy *= drag;
        p.life -= dt;
        if (p.life <= 0) {
            m_particles[i - 1] = m_particles.back();
            m_particles.pop_back();
        }
    }
}

void ParticleSystem::Draw(SpriteBatch& batch) {
    for (auto& p : m_particles) {
        float lifeRatio = p.life / p.maxLife;
        float alpha = lifeRatio * p.a;
        float sz = p.size;
        if (p.flags & 1) sz *= (0.5f + 0.5f * lifeRatio); // glow shrinks
        batch.Draw(
            {p.x - sz * 0.5f, p.y - sz * 0.5f},
            {sz, sz},
            {p.r, p.g, p.b, alpha}
        );
    }
}

} // namespace pfd
