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
    std::uniform_real_distribution<float> distLife(2.0f, 3.0f);
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
        p.orbitRadius = 0.0f;
    }
}

void ParticleSystem::EmitContinuous(float centerX, float y, float noteW, util::Color color) {
    if (m_activeCount >= MAX_PARTICLES - 2) return;
    
    std::uniform_real_distribution<float> distSpeed(80.0f, 150.0f);
    std::uniform_real_distribution<float> distGlow(0.0f, 1.0f);
    std::uniform_real_distribution<float> distOrbitSpeed(4.0f, 8.0f);
    std::uniform_real_distribution<float> distRadius(0.0f, 8.0f);

    // Left Edge Particle
    {
        float life = 1.5f + distGlow(m_rng) * 1.0f;
        bool isGlow = distGlow(m_rng) < 0.3f;
        auto& p = AllocateParticle();
        p.x = centerX - noteW * 0.5f;
        p.y = y;
        p.vx = 0;
        p.vy = -distSpeed(m_rng);
        p.r = std::min(1.0f, color.r * 1.5f); 
        p.g = std::min(1.0f, color.g * 1.5f);
        p.b = std::min(1.0f, color.b * 1.5f);
        p.a = 1.0f;
        p.life = life;
        p.maxLife = life;
        p.size = isGlow ? (3.0f + distGlow(m_rng) * 5.0f) : (1.5f + distGlow(m_rng) * 2.0f);
        p.flags = isGlow ? 1u : 0u;
        p.anchorX = centerX;
        p.orbitPhase = 3.14159f; // Start on left side
        p.orbitSpeed = distOrbitSpeed(m_rng);
        p.orbitRadius = noteW * 0.5f + distRadius(m_rng);
    }
    
    // Right Edge Particle
    {
        float life = 1.5f + distGlow(m_rng) * 1.0f;
        bool isGlow = distGlow(m_rng) < 0.3f;
        auto& p = AllocateParticle();
        p.x = centerX + noteW * 0.5f;
        p.y = y;
        p.vx = 0;
        p.vy = -distSpeed(m_rng);
        p.r = std::min(1.0f, color.r * 1.5f); 
        p.g = std::min(1.0f, color.g * 1.5f);
        p.b = std::min(1.0f, color.b * 1.5f);
        p.a = 1.0f;
        p.life = life;
        p.maxLife = life;
        p.size = isGlow ? (3.0f + distGlow(m_rng) * 5.0f) : (1.5f + distGlow(m_rng) * 2.0f);
        p.flags = isGlow ? 1u : 0u;
        p.anchorX = centerX;
        p.orbitPhase = 0.0f; // Start on right side
        p.orbitSpeed = distOrbitSpeed(m_rng);
        p.orbitRadius = noteW * 0.5f + distRadius(m_rng);
    }

    // Optional Smoke Particle (occasional)
    if (distGlow(m_rng) < 0.12f) {
        float life = 2.0f + distGlow(m_rng) * 1.5f;
        auto& p = AllocateParticle();
        p.x = centerX + (distGlow(m_rng) - 0.5f) * noteW;
        p.y = y;
        p.vx = (distGlow(m_rng) - 0.5f) * 15.0f;
        p.vy = -120.0f - distSpeed(m_rng) * 0.3f;
        p.r = color.r; p.g = color.g; p.b = color.b; p.a = 0.15f;
        p.life = life;
        p.maxLife = life;
        p.size = 4.0f + distGlow(m_rng) * 4.0f;
        p.flags = 2u; // Bit 1 = IsSmoke
        p.orbitRadius = 0.0f;
    }
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
        p.maxLife = 1.0f + distSize(m_rng) * 0.5f;
        p.life = p.maxLife;
        p.size = 1.0f + distSize(m_rng);
        p.flags = 0u;
        p.orbitRadius = 0.0f;
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
        
        // Orbit logic for continuous trailing particles
        if (p.orbitRadius > 0.0f) {
            float lifeRatio = p.life / p.maxLife; // 1.0 at birth, 0.0 at death
            // Orbit pull weakens as the particle ages — they slowly escape
            float orbitStrength = lifeRatio * lifeRatio; // quadratic falloff
            
            p.orbitPhase += p.orbitSpeed * dt;
            // Helix effect: orbit around anchorX
            float targetX = p.anchorX + std::cosf(p.orbitPhase) * p.orbitRadius;
            // Pull strength fades with age so particles drift free
            p.x += (targetX - p.x) * 15.0f * orbitStrength * dt;
            
            // Random jitter/turbulence added to velocity
            float swirlX = std::sinf(p.y * 0.05f + time * 3.0f) * 40.0f;
            float swirlY = std::cosf(p.x * 0.05f + time * 2.0f) * 30.0f;
            p.vx += swirlX * dt;
            p.vy += swirlY * dt;
            
            p.vy += gravity * dt;
            if (gravity == 0.0f) p.vy -= 250.0f * dt; // Strong rise
            
            p.y += p.vy * dt;
            p.x += p.vx * dt;
            
            float drag = 1.0f - (1.0f * dt);
            if (drag < 0) drag = 0;
            p.vy *= drag;
            p.vx *= drag;
            
            // Gradually expand the orbit as they rise
            p.orbitRadius += 15.0f * dt;
        } else {
            // Normal particle physics (bursts, sparks, smoke)
            if (p.flags & 2) { // SMOKE PHYSICS
                p.vy -= 120.0f * dt; // Rise faster
                p.x += std::sinf(time * 1.5f + p.life) * 12.0f * dt; // Subtle drifting wobble
                p.size += 14.0f * dt; // Expand more slowly
                p.y += p.vy * dt;
                p.x += p.vx * dt;
                p.vx *= (1.0f - 0.8f * dt);
                p.vy *= (1.0f - 0.8f * dt);
            } else {
                // 1. Vortex Turbulence
                float noiseScale = 0.015f;
                float noiseSpeed = 2.0f;
                float nx = p.x * noiseScale;
                float ny = p.y * noiseScale;
                
                float swirlX = std::sinf(ny + time * noiseSpeed) * std::cosf(nx - time) * 120.0f;
                float swirlY = std::cosf(nx + time * noiseSpeed) * std::sinf(ny + time) * 120.0f;
                
                p.vx += swirlX * dt;
                p.vy += swirlY * dt;
                
                // 2. Base Drift
                p.vy += gravity * dt;
                if (gravity == 0.0f) p.vy -= 150.0f * dt;
                
                p.x  += p.vx * dt;
                p.y  += p.vy * dt;
                
                // Drag
                float drag = 1.0f - (1.2f * dt);
                if (drag < 0) drag = 0;
                p.vx *= drag;
                p.vy *= drag;
            }
        }
        
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
        
        if (p.flags & 2) { // SMOKE DRAWING
            // Smoke fades in then out, very subtly
            float alpha = p.a * std::sinf(lifeRatio * 3.14159f) * 0.25f;
            batch.Draw(
                {p.x - p.size * 0.5f, p.y - p.size * 0.5f},
                {p.size, p.size},
                {p.r, p.g, p.b, alpha}
            );
        } else {
            // Smooth fade out for sparks
            float alpha = p.a * std::pow(lifeRatio, 1.2f);
            float sz = p.size;
            if (p.flags & 1) sz *= (0.5f + 0.5f * lifeRatio); // glow shrinks
            
            // 1. Core Particle
            batch.Draw(
                {p.x - sz * 0.5f, p.y - sz * 0.5f},
                {sz, sz},
                {p.r, p.g, p.b, alpha}
            );

            // 2. Bloom Aura (subtle, disappears faster than the core)
            float bloomSz = sz * 2.0f;
            float bloomAlpha = alpha * 0.15f * std::pow(lifeRatio, 1.5f);
            if (bloomAlpha > 0.01f) {
                batch.Draw(
                    {p.x - bloomSz * 0.5f, p.y - bloomSz * 0.5f},
                    {bloomSz, bloomSz},
                    {p.r, p.g, p.b, bloomAlpha}
                );
            }
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

} // namespace pfd
