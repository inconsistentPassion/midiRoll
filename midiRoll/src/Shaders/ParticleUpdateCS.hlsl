// ParticleUpdateCS.hlsl — GPU particle simulation compute shader
// Replaces the entire CPU ParticleSystem::Update() loop.

struct Particle {
    float2 pos;
    float2 vel;
    float4 color;
    float  life;
    float  maxLife;
    float  size;
    uint   flags;      // bit0=isGlow, bit1=isSmoke
    float  anchorX;
    float  orbitSpeed;
    float  orbitRadius;
    float  orbitPhase;
};

struct EmitParticle {
    float2 pos;
    float2 vel;
    float4 color;
    float  life;
    float  maxLife;
    float  size;
    uint   flags;
    float  anchorX;
    float  orbitSpeed;
    float  orbitRadius;
    float  orbitPhase;
};

RWStructuredBuffer<Particle>      g_particles   : register(u0);
AppendStructuredBuffer<EmitParticle> g_emitAppend : register(u1);
RWBuffer<uint>                    g_counter     : register(u2);  // [0] = particle count

cbuffer CBFrame : register(b0) {
    float  g_dt;
    float  g_gravity;
    float  g_time;       // steady_clock seconds (for noise)
    float  g_emberMode;  // 0 or 1
    uint   g_maxParticles;
    float3 g_pad;
};

// ---- Noise helpers ----
float hash(float n) { return frac(sin(n) * 43758.5453123); }

float noise2d(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = i.x + i.y * 57.0;
    return lerp(
        lerp(hash(n),       hash(n + 1.0),   f.x),
        lerp(hash(n + 57.0), hash(n + 58.0), f.x),
        f.y
    );
}

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= g_maxParticles) return;

    Particle p = g_particles[idx];
    if (p.life <= 0.0) return;  // dead particle, skip

    float gravity = g_emberMode > 0.5 ? -180.0 : g_gravity;
    float time = g_time;

    // Orbit logic
    if (p.orbitRadius > 0.0) {
        float lifeRatio = p.life / p.maxLife;
        float orbitStrength = lifeRatio * lifeRatio;

        p.orbitPhase += p.orbitSpeed * g_dt;
        float targetX = p.anchorX + cos(p.orbitPhase) * p.orbitRadius;
        p.pos.x += (targetX - p.pos.x) * 15.0 * orbitStrength * g_dt;

        // Turbulence
        float swirlX = sin(p.pos.y * 0.05 + time * 3.0) * 40.0;
        float swirlY = cos(p.pos.x * 0.05 + time * 2.0) * 30.0;
        p.vel.x += swirlX * g_dt;
        p.vel.y += swirlY * g_dt;

        p.vel.y += gravity * g_dt;
        if (gravity == 0.0) p.vel.y -= 250.0 * g_dt;

        p.pos.y += p.vel.y * g_dt;
        p.pos.x += p.vel.x * g_dt;

        float drag = 1.0 - g_dt;
        if (drag < 0.0) drag = 0.0;
        p.vel.y *= drag;
        p.vel.x *= drag;

        p.orbitRadius += 15.0 * g_dt;
    }
    // Smoke physics
    else if (p.flags & 2u) {
        p.vel.y -= 120.0 * g_dt;
        p.pos.x += sin(time * 1.5 + p.life) * 12.0 * g_dt;
        p.size += 14.0 * g_dt;
        p.pos.y += p.vel.y * g_dt;
        p.pos.x += p.vel.x * g_dt;
        p.vel.x *= (1.0 - 0.8 * g_dt);
        p.vel.y *= (1.0 - 0.8 * g_dt);
    }
    // Normal particle physics (bursts, sparks)
    else {
        float noiseScale = 0.015;
        float noiseSpeed = 2.0;
        float nx = p.pos.x * noiseScale;
        float ny = p.pos.y * noiseScale;

        float swirlX = sin(ny + time * noiseSpeed) * cos(nx - time) * 120.0;
        float swirlY = cos(nx + time * noiseSpeed) * sin(ny + time) * 120.0;

        p.vel.x += swirlX * g_dt;
        p.vel.y += swirlY * g_dt;

        p.vel.y += gravity * g_dt;
        if (gravity == 0.0) p.vel.y -= 150.0 * g_dt;

        p.pos.x += p.vel.x * g_dt;
        p.pos.y += p.vel.y * g_dt;

        float drag = 1.0 - 1.2 * g_dt;
        if (drag < 0.0) drag = 0.0;
        p.vel.x *= drag;
        p.vel.y *= drag;
    }

    // Decrease lifetime
    p.life -= g_dt;

    // Write back (dead particles get life<=0, will be skipped next frame)
    g_particles[idx] = p;
}

// Separate kernel for uploading emitted particles
[numthreads(256, 1, 1)]
void CSEmit(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    // Handled by CPU via AppendStructuredBuffer — this shader is unused
    // but kept as a placeholder if we want GPU-side emission later.
}
