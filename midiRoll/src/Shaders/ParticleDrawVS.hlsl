// ParticleDrawVS.hlsl — Vertex shader for GPU particle rendering
// Reads particle data from StructuredBuffer instead of per-instance VB.

struct Particle {
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

StructuredBuffer<Particle> g_particles : register(t0);

cbuffer CBPerFrame : register(b0) {
    float viewWidth;
    float viewHeight;
    float2 pad;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

// Vertex input: just position + UV (quad vertices)
// Instance ID comes from SV_InstanceID
VSOut VSMain(float2 vPos : POSITION, float2 vUV : TEXCOORD0, uint instID : SV_InstanceID) {
    Particle p = g_particles[instID];

    float lifeRatio = p.life / p.maxLife;
    float sz = p.size;

    // Glow shrinks with age
    if (p.flags & 1u) {
        sz *= (0.5 + 0.5 * lifeRatio);
    }

    float alpha;
    if (p.flags & 2u) {
        // Smoke: fade in then out
        alpha = p.color.a * sin(lifeRatio * 3.14159) * 0.25;
    } else {
        // Normal: smooth fade out
        alpha = p.color.a * pow(lifeRatio, 1.2);
    }

    // Position: particle center + quad vertex * size
    float2 halfSize = float2(sz * 0.5, sz * 0.5);
    float2 worldPos = p.pos + (vPos - 0.5) * sz;

    // Pixel to NDC
    float2 ndc;
    ndc.x = (worldPos.x / viewWidth) * 2.0 - 1.0;
    ndc.y = 1.0 - (worldPos.y / viewHeight) * 2.0;

    VSOut o;
    o.pos   = float4(ndc, 0, 1);
    o.uv    = vUV;
    o.color = float4(p.color.rgb, alpha);
    return o;
}
