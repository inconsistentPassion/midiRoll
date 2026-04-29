#include "ParticleSystem.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace pfd {

bool ParticleSystem::Initialize(ID3D11Device*) {
    // Pre-allocate particle pool to avoid runtime allocations
    m_particles.resize(MAX_PARTICLES);
    m_activeCount = 0;
    
    // Initialize all particles as inactive
    for (auto& p : m_particles) {
        p.active = false;
        p.life = 0.0f;
    }
    
    return true;
}

Particle& ParticleSystem::AllocateParticle() {
    // Ring-search from last allocation position to avoid O(N) scan from index 0
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        size_t idx = (m_nextSearchIdx + i) % MAX_PARTICLES;
        if (!m_particles[idx].active) {
            m_particles[idx].active = true;
            m_activeCount++;
            m_nextSearchIdx = (idx + 1) % MAX_PARTICLES;
            return m_particles[idx];
        }
    }

    // Pool is full - write into a discarded dummy particle so callers never
    // corrupt existing live particles and we never corrupt m_activeCount.
    m_dummyParticle = {};
    m_dummyParticle.active = false; // won't be updated or drawn
    return m_dummyParticle;
}

size_t ParticleSystem::Count() const {
    return m_activeCount;
}

void ParticleSystem::Clear() {
    for (auto& p : m_particles) {
        p.active = false;
    }
    m_activeCount = 0;
    m_nextSearchIdx = 0;
}

void ParticleSystem::EmitBurst(float x, float y, util::Color color, int count) {
    std::uniform_real_distribution<float> distAngle(-1.57f * 1.2f, -1.57f * 0.4f);
    std::uniform_real_distribution<float> distSpeed(80.0f, 300.0f);
    std::uniform_real_distribution<float> distSize(1.0f, 4.0f);
    std::uniform_real_distribution<float> distGlowSize(3.0f, 8.0f);
    std::uniform_real_distribution<float> distLife(0.5f, 1.2f);
    std::uniform_real_distribution<float> distOffset(-3.0f, 3.0f);

    for (int i = 0; i < count; ++i) {
        if (m_activeCount >= MAX_PARTICLES) break;
        
        bool isGlow = std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.4f;
        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);
        float sz = isGlow ? distGlowSize(m_rng) : distSize(m_rng);

        auto& p = AllocateParticle();
        p.x = x + distOffset(m_rng);
        p.y = y;
        p.vx = std::cosf(angle) * speed;
        p.vy = std::sinf(angle) * speed;
        p.r = color.r;
        p.g = color.g;
        p.b = color.b;
        p.a = color.a;
        p.life = distLife(m_rng);
        p.maxLife = p.life;
        p.size = sz;
        p.flags = isGlow ? 1u : 0u;
    }
}

void ParticleSystem::EmitContinuous(float x, float y, util::Color color) {
    if (m_activeCount >= MAX_PARTICLES) return;
    
    std::uniform_real_distribution<float> distAngle(-1.8f, -1.35f);
    std::uniform_real_distribution<float> distSpeed(50.0f, 170.0f);
    std::uniform_real_distribution<float> distOffset(-6.0f, 6.0f);
    std::uniform_real_distribution<float> distYOffset(-2.0f, 2.0f);
    std::uniform_real_distribution<float> distGlowAdd(0.0f, 4.0f);
    std::uniform_real_distribution<float> distSizeAdd(0.0f, 2.0f);

    float life = m_emberMode ? 2.5f : 1.0f;
    float angle = distAngle(m_rng);
    float speed = distSpeed(m_rng);
    bool isGlow = std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.3f;

    auto& p = AllocateParticle();
    p.x = x + distOffset(m_rng);
    p.y = y + distYOffset(m_rng);
    p.vx = std::cosf(angle) * speed;
    p.vy = std::sinf(angle) * speed;
    p.r = color.r;
    p.g = color.g;
    p.b = color.b;
    p.a = color.a;
    p.life = life;
    p.maxLife = life;
    p.size = isGlow ? (2.0f + distGlowAdd(m_rng)) : (1.0f + distSizeAdd(m_rng));
    p.flags = isGlow ? 1u : 0u;
}

void ParticleSystem::EmitSparks(float x, float y, util::Color color) {
    std::uniform_real_distribution<float> distAngle(0, 6.28318f);
    std::uniform_real_distribution<float> distSpeed(180.0f, 530.0f);
    std::uniform_real_distribution<float> distSize(0.0f, 1.5f);

    for (int i = 0; i < 12; ++i) {
        if (m_activeCount >= MAX_PARTICLES) break;
        
        float angle = distAngle(m_rng);
        float speed = distSpeed(m_rng);

        auto& p = AllocateParticle();
        p.x = x;
        p.y = y;
        p.vx = std::cosf(angle) * speed;
        p.vy = std::sinf(angle) * speed - 60.0f;
        p.r = color.r;
        p.g = color.g;
        p.b = color.b;
        p.a = color.a;
        p.life = 0.3f;
        p.maxLife = 0.6f;
        p.size = 1.0f + distSize(m_rng);
        p.flags = 0u;
    }
}

void ParticleSystem::Update(float dt) {
    float gravity = m_emberMode ? -180.0f : m_gravity;
    float time = (float)std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Iterate through pool and update active particles
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        auto& p = m_particles[i];
        if (!p.active) continue;
        
        // Sway effect
        p.vx += std::sinf(time * 3.0f + p.y * 0.01f) * 3.5f * dt * 10.0f;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.vy += gravity * dt;
        
        // Drag
        float drag = (p.flags & 1) ? 0.97f : 0.99f;
        p.vx *= drag;
        p.vy *= drag;
        
        // Decrease lifetime
        p.life -= dt;
        if (p.life <= 0) {
            // Deactivate instead of removing
            p.active = false;
            m_activeCount--;
        }
    }
}

void ParticleSystem::Draw(SpriteBatch& batch) {
    // Only draw active particles
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        const auto& p = m_particles[i];
        if (!p.active) continue;
        
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
