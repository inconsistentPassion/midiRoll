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
    
    std::uniform_real_distribution<float> distAngle(-2.4f, -0.7f); // Much wider dispersion arc
    std::uniform_real_distribution<float> distSpeed(150.0f, 450.0f); // Faster initial burst
    std::uniform_real_distribution<float> distOffset(-12.0f, 12.0f); // Wider spawn area
    std::uniform_real_distribution<float> distGlow(0.0f, 1.0f);

    float life = 0.8f + distGlow(m_rng) * 0.4f;
    float angle = distAngle(m_rng);
    float speed = distSpeed(m_rng);
    bool isGlow = distGlow(m_rng) < 0.25f;

    auto& p = AllocateParticle();
    p.x = x + distOffset(m_rng);
    p.y = y;
    p.vx = std::cosf(angle) * speed;
    p.vy = std::sinf(angle) * speed;
    p.r = std::min(1.0f, color.r * 1.5f); 
    p.g = std::min(1.0f, color.g * 1.5f);
    p.b = std::min(1.0f, color.b * 1.5f);
    p.a = 1.0f;
    p.life = life;
    p.maxLife = life;
    p.size = isGlow ? (4.0f + distGlow(m_rng) * 6.0f) : (1.5f + distGlow(m_rng) * 2.5f);
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
        
        // 1. Eddy Currents (Turbulent Swirls)
        float swirlX = std::sinf(time * 4.0f + p.y * 0.05f) * 60.0f;
        float swirlY = std::cosf(time * 3.0f + p.x * 0.05f) * 40.0f;
        
        p.vx += swirlX * dt;
        p.vy += swirlY * dt;
        
        // 2. Upward Drift (instead of gravity)
        p.vy -= 450.0f * dt; // Much stronger upward push
        
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        
        // Drag (slightly less to let swirls build up)
        float drag = (p.flags & 1) ? 0.94f : 0.96f;
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
    batch.SetBlendMode(true);
    batch.SetTexture(m_texture);
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        const auto& p = m_particles[i];
        if (!p.active) continue;
        
        float lifeRatio = p.life / p.maxLife;
        float alpha = lifeRatio * p.a;
        float sz = p.size;
        if (p.flags & 1) sz *= (0.5f + 0.5f * lifeRatio); // glow shrinks
        
        // 1. Core Particle
        batch.Draw(
            {p.x - sz * 0.5f, p.y - sz * 0.5f},
            {sz, sz},
            {p.r, p.g, p.b, alpha}
        );

        // 2. Bloom Aura (disappears faster than the core)
        float bloomSz = sz * 3.5f;
        float bloomAlpha = alpha * 0.35f * std::pow(lifeRatio, 1.5f);
        if (bloomAlpha > 0.01f) {
            batch.Draw(
                {p.x - bloomSz * 0.5f, p.y - bloomSz * 0.5f},
                {bloomSz, bloomSz},
                {p.r, p.g, p.b, bloomAlpha}
            );
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

} // namespace pfd
