// particle.hlsl — GPU compute shader for particle simulation
// Not yet wired to the CPU-side ParticleSystem (which uses CPU update for now).
// This is here for when you migrate to GPU particles.

struct Particle {
    float2 position;
    float2 velocity;
    float4 color;
    float  life;
    float  maxLife;
    float  size;
    uint   flags;
};

RWStructuredBuffer<Particle> Particles : register(u0);

cbuffer SimParams : register(b0) {
    float  deltaTime;
    float  gravity;
    float  time;
    float  padding;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    Particle p = Particles[id.x];
    if (p.life <= 0) return;

    // Horizontal sway
    p.velocity.x += sin(time * 3.0 + p.position.y * 0.01) * 35.0 * deltaTime;

    p.position += p.velocity * deltaTime;
    p.velocity.y += gravity * deltaTime;

    // Air resistance
    float drag = (p.flags & 1) ? 0.97 : 0.99;
    p.velocity *= drag;

    p.life -= deltaTime;

    Particles[id.x] = p;
}
