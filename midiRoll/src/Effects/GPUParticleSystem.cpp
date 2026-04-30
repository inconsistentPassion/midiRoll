#include "GPUParticleSystem.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace pfd {

// ─────────────────────────────────────────────────────────────────────────────
// HLSL — compute shader for particle simulation
// ─────────────────────────────────────────────────────────────────────────────
static const char* g_csSource = R"(
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

RWStructuredBuffer<Particle> g_particles : register(u0);

cbuffer CBUpdate : register(b0) {
    float  g_dt;
    float  g_gravity;
    float  g_time;
    float  g_emberMode;
    uint   g_maxParticles;
    float3 g_pad;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= g_maxParticles) return;

    Particle p = g_particles[idx];
    if (p.life <= 0.0) return;

    // Safety: ensure g_dt is valid
    float dt = max(g_dt, 0.0001);
    float gravity = g_emberMode > 0.5 ? -180.0 : g_gravity;
    float time = g_time;

    float age = p.maxLife - p.life;

    if (p.orbitRadius > 0.0) {
        float lifeRatio = p.life / p.maxLife;
        float orbitStrength = lifeRatio * lifeRatio;
        p.orbitPhase += p.orbitSpeed * dt;
        
        if (age < 2.0) {
            // PHASE 1: Classic Helix Orbit
            float targetX = p.anchorX + cos(p.orbitPhase) * p.orbitRadius;
            p.pos.x += (targetX - p.pos.x) * 15.0 * orbitStrength * dt;
            float swirlX = sin(p.pos.y * 0.05 + time * 3.0) * 40.0;
            float swirlY = cos(p.pos.x * 0.05 + time * 2.0) * 30.0;
            p.vel.x += swirlX * dt;
            p.vel.y += swirlY * dt;
            p.vel.y += (gravity - 250.0) * dt;
        } else {
            // PHASE 2: 2D Fluid Dispersal (Navier-Stokes / Curl-Noise Approximation)
            float ns = 0.01; // Noise scale
            float nt = time * 1.5;
            // 2-Octave Curl Noise for fluid vortices
            float v1x = sin(p.pos.y * ns + nt) * 100.0;
            float v1y = cos(p.pos.x * ns + nt) * 100.0;
            float v2x = sin(p.pos.y * ns * 2.5 - nt * 0.7) * 50.0;
            float v2y = cos(p.pos.x * ns * 2.5 + nt * 1.2) * 50.0;
            
            p.vel.x += (v1x + v2x) * dt;
            p.vel.y += (v1y + v2y) * dt;
            p.vel.y += (gravity - 100.0) * dt; // Slower rise, more drift
            
            p.size += 5.0 * dt; // Diffusion expansion
            p.color.a *= (1.0 - 0.4 * dt); // Faster fade during dispersal
        }

        p.pos.y += p.vel.y * dt;
        p.pos.x += p.vel.x * dt;
        float drag = 1.0 - (age < 2.0 ? 1.0 : 0.4) * dt; 
        if (drag < 0.0) drag = 0.0;
        p.vel.y *= drag;
        p.vel.x *= drag;
        p.orbitRadius += 15.0 * dt;
    }
    else if (p.flags & 2u) {
        p.vel.y -= 120.0 * dt; // Original 120.0
        p.pos.x += sin(time * 1.5 + p.life) * 12.0 * dt;
        p.size += 14.0 * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.x += p.vel.x * dt;
        p.vel.x *= (1.0 - 0.8 * dt);
        p.vel.y *= (1.0 - 0.8 * dt);
    }
    else {
        float noiseScale = 0.015;
        float noiseSpeed = 2.0;
        float nx = p.pos.x * noiseScale;
        float ny = p.pos.y * noiseScale;
        float swirlX = sin(ny + time * noiseSpeed) * cos(nx - time) * 120.0;
        float swirlY = cos(nx + time * noiseSpeed) * sin(ny + time) * 120.0;
        p.vel.x += swirlX * dt;
        p.vel.y += swirlY * dt;
        p.vel.y += gravity * dt;
        if (gravity == 0.0) p.vel.y -= 150.0 * dt; // Original 150.0
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        float drag = 1.0 - 1.2 * dt; // Original 1.2
        if (drag < 0.0) drag = 0.0;
        p.vel.x *= drag;
        p.vel.y *= drag;
    }

    p.life -= g_dt;
    g_particles[idx] = p;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// HLSL — vertex shader for drawing particles from StructuredBuffer
// ─────────────────────────────────────────────────────────────────────────────
static const char* g_vsSource = R"(
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

cbuffer CBDraw : register(b0) {
    float viewWidth;
    float viewHeight;
    float2 pad2;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(float2 vPos : POSITION, float2 vUV : TEXCOORD0, uint instID : SV_InstanceID) {
    Particle p = g_particles[instID];

    float lifeRatio = p.life / p.maxLife;
    float sz = p.size;
    if (p.flags & 1u) sz *= (0.5 + 0.5 * lifeRatio);

    float alpha;
    if (p.flags & 2u) {
        alpha = p.color.a * sin(lifeRatio * 3.14159) * 0.25;
    } else {
        alpha = p.color.a * pow(lifeRatio, 1.2);
    }

    // Dead particles: degenerate quad (zero size, zero alpha)
    if (p.life <= 0.0) {
        VSOut o;
        o.pos = float4(0, 0, 0, 1);
        o.uv = float2(0, 0);
        o.color = float4(0, 0, 0, 0);
        return o;
    }

    float2 worldPos = p.pos + (vPos - 0.5) * sz;

    float2 ndc;
    ndc.x = (worldPos.x / viewWidth) * 2.0 - 1.0;
    ndc.y = 1.0 - (worldPos.y / viewHeight) * 2.0;

    VSOut o;
    o.pos   = float4(ndc, 0, 1);
    o.uv    = vUV;
    o.color = float4(p.color.rgb, alpha);
    return o;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// HLSL — pixel shader for particle rendering
// ─────────────────────────────────────────────────────────────────────────────
static const char* g_psSource = R"(
Texture2D    tex : register(t0);
SamplerState sam : register(s0);

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

float4 PSMain(PSIn input) : SV_TARGET {
    float4 texColor = tex.Sample(sam, input.uv);
    
    float dist = length(input.uv - 0.5);
    float core = texColor.r;
    
    // Intensified Bloom for sparks (flags == 0 usually)
    float auraPower = (dist < 0.1) ? 1.5 : 1.0; 
    float aura = exp(-dist * 5.0) * 0.25 * auraPower;
    
    float mask = max(core * 1.2, aura); // Boosted brightness
    return float4(input.color.rgb, input.color.a * mask);
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// Helper
// ─────────────────────────────────────────────────────────────────────────────
static ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
               entry, target, 0, 0, blob.GetAddressOf(), err.GetAddressOf());
    if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
    return blob;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialize / Shutdown
// ─────────────────────────────────────────────────────────────────────────────
bool GPUParticleSystem::Initialize(ID3D11Device* device) {
    CreateBuffers(device);
    CreateShaders(device);
    m_initialized = true;
    return true;
}

void GPUParticleSystem::Shutdown() {
    m_particleBuffer.Reset(); m_particleUAV.Reset(); m_particleSRV.Reset();
    m_stagingBuffer.Reset();
    m_cbUpdate.Reset(); m_cbDraw.Reset();
    m_csUpdate.Reset();
    m_vsDraw.Reset(); m_psDraw.Reset(); m_drawLayout.Reset();
    m_quadVB.Reset(); m_quadIB.Reset();
    m_additiveBlend.Reset(); m_rasterizer.Reset();
    m_depthStencil.Reset(); m_sampler.Reset();
    m_initialized = false;
}

void GPUParticleSystem::CreateBuffers(ID3D11Device* device) {
    // ── Particle buffer: DEFAULT + UAV + SRV ──
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = MAX_PARTICLES * sizeof(GPUParticle);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.StructureByteStride = sizeof(GPUParticle);
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        std::vector<GPUParticle> zeros(MAX_PARTICLES);
        memset(zeros.data(), 0, zeros.size() * sizeof(GPUParticle));
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = zeros.data();
        device->CreateBuffer(&bd, &init, m_particleBuffer.GetAddressOf());

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = MAX_PARTICLES;
        device->CreateUnorderedAccessView(m_particleBuffer.Get(), &uav, m_particleUAV.GetAddressOf());

        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = MAX_PARTICLES;
        device->CreateShaderResourceView(m_particleBuffer.Get(), &srv, m_particleSRV.GetAddressOf());
    }

    // ── Staging buffer: STAGING + CPU_READ | CPU_WRITE ──
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = MAX_PARTICLES * sizeof(GPUParticle);
        bd.Usage = D3D11_USAGE_STAGING;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&bd, nullptr, m_stagingBuffer.GetAddressOf());
    }

    // ── Constant buffers ──
    {
        // CB for compute: dt, gravity, time, emberMode, maxParticles, pad[3]
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 32; // 8 floats
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        device->CreateBuffer(&bd, nullptr, m_cbUpdate.GetAddressOf());

        // CB for draw: viewWidth, viewHeight, pad[2]
        bd.ByteWidth = 16;
        device->CreateBuffer(&bd, nullptr, m_cbDraw.GetAddressOf());
    }

    // ── Quad geometry ──
    {
        struct V { float x, y, u, v; };
        V verts[] = {{0,0,0,0},{1,0,1,0},{1,1,1,1},{0,1,0,1}};
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(verts);
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = verts;
        device->CreateBuffer(&bd, &init, m_quadVB.GetAddressOf());

        uint16_t idx[] = {0,1,2,0,2,3};
        bd.ByteWidth = sizeof(idx);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        init.pSysMem = idx;
        device->CreateBuffer(&bd, &init, m_quadIB.GetAddressOf());
    }

    // ── Blend / rasterizer / depth / sampler ──
    {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&bd, m_additiveBlend.GetAddressOf());

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState(&rd, m_rasterizer.GetAddressOf());

        D3D11_DEPTH_STENCIL_DESC dsd{};
        dsd.DepthEnable = FALSE;
        device->CreateDepthStencilState(&dsd, m_depthStencil.GetAddressOf());

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxAnisotropy = 4;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        device->CreateSamplerState(&sd, m_sampler.GetAddressOf());
    }
}

void GPUParticleSystem::CreateShaders(ID3D11Device* device) {
    auto cs = Compile(g_csSource, "CSMain", "cs_5_0");
    if (cs) device->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(),
                                         nullptr, m_csUpdate.GetAddressOf());

    auto vs = Compile(g_vsSource, "VSMain", "vs_5_0");
    if (vs) {
        device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                    nullptr, m_vsDraw.GetAddressOf());
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device->CreateInputLayout(layout, 2, vs->GetBufferPointer(),
                                   vs->GetBufferSize(), m_drawLayout.GetAddressOf());
    }

    auto ps = Compile(g_psSource, "PSMain", "ps_5_0");
    if (ps) device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                       nullptr, m_psDraw.GetAddressOf());
}

// ─────────────────────────────────────────────────────────────────────────────
// Emit functions — CPU-side, queue into m_emitQueue
// ─────────────────────────────────────────────────────────────────────────────
void GPUParticleSystem::EmitBurst(float x, float y, util::Color color, int count) {
    if (m_frameEmitCount >= MAX_FRAME_EMIT) return;
    count = std::min(count, (int)(MAX_FRAME_EMIT - m_frameEmitCount));
    m_frameEmitCount += count;
    std::uniform_real_distribution<float> da(-1.57f*1.2f, -1.57f*0.4f);
    std::uniform_real_distribution<float> ds(80.f, 300.f); // Classic Burst Speed
    std::uniform_real_distribution<float> dz(1.f, 4.f);
    std::uniform_real_distribution<float> dg(3.f, 8.f);
    std::uniform_real_distribution<float> dl(2.f, 3.f);
    std::uniform_real_distribution<float> dn(-3.f, 3.f);
    std::uniform_real_distribution<float> coin(0.f, 1.f);

    for (int i = 0; i < count; ++i) {
        bool glow = coin(m_rng) < 0.4f;
        float a = da(m_rng), sp = ds(m_rng);
        float sz = glow ? dg(m_rng) : dz(m_rng);
        EmitParticle ep{};
        ep.x = x + dn(m_rng); ep.y = y;
        ep.vx = cosf(a)*sp;   ep.vy = sinf(a)*sp;
        ep.r = color.r; ep.g = color.g; ep.b = color.b; ep.a = color.a;
        ep.life = dl(m_rng); ep.maxLife = ep.life; ep.size = sz;
        ep.flags = glow ? 1u : 0u; ep.orbitRadius = 0.f;
        m_emitQueue.push_back(ep);
    }
}

void GPUParticleSystem::EmitContinuous(float cx, float y, float noteW, util::Color color) {
    if (m_frameEmitCount >= MAX_FRAME_EMIT) return;
    m_frameEmitCount += 8; // 2 sides * 4 particles
    std::uniform_real_distribution<float> ds(80.f, 150.f); // Classic Continuous Speed
    std::uniform_real_distribution<float> coin(0.f, 1.f);
    std::uniform_real_distribution<float> dos(4.f, 8.f);
    std::uniform_real_distribution<float> dr(0.f, 8.f);

    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 4; i++) {
            float life = 1.5f + coin(m_rng);
            bool glow = coin(m_rng) < 0.6f;
            EmitParticle ep{};
            ep.x = cx + (side == 0 ? -1.f : 1.f) * noteW * 0.5f;
            ep.y = y; ep.vx = 0; ep.vy = -ds(m_rng);
            ep.r = std::min(1.f, color.r*1.8f);
            ep.g = std::min(1.f, color.g*1.8f);
            ep.b = std::min(1.f, color.b*1.8f);
            ep.a = 1.f;
            ep.life = life; ep.maxLife = life;
            ep.size = glow ? (4.f + coin(m_rng)*4.f) : (2.f + coin(m_rng)*2.f);
            ep.flags = glow ? 1u : 0u;
            ep.anchorX = cx;
            ep.orbitPhase = (side == 0 ? 3.14159f : 0.f) + (float)i * 1.57f;
            ep.orbitSpeed = dos(m_rng);
            ep.orbitRadius = noteW * 0.5f + dr(m_rng);
            m_emitQueue.push_back(ep);
        }
    }

    if (coin(m_rng) < 0.12f) {
        float life = 2.f + coin(m_rng) * 1.5f;
        EmitParticle ep{};
        ep.x = cx + (coin(m_rng) - 0.5f) * noteW;
        ep.y = y;
        ep.vx = (coin(m_rng) - 0.5f) * 15.f;
        ep.vy = -120.f - ds(m_rng) * 0.3f;
        ep.r = color.r; ep.g = color.g; ep.b = color.b; ep.a = 0.15f;
        ep.life = life; ep.maxLife = life;
        ep.size = 4.f + coin(m_rng) * 4.f;
        ep.flags = 2u; ep.orbitRadius = 0.f;
        m_emitQueue.push_back(ep);
    }
}

void GPUParticleSystem::EmitSparks(float x, float y, util::Color color) {
    std::uniform_real_distribution<float> da(0.f, 6.28318f);
    std::uniform_real_distribution<float> ds(180.f, 530.f); 
    std::uniform_real_distribution<float> dz(2.5f, 6.0f); // Larger Sparks

    for (int i = 0; i < 12; ++i) {
        float a = da(m_rng), sp = ds(m_rng);
        EmitParticle ep{};
        ep.x = x; ep.y = y;
        ep.vx = cosf(a)*sp; ep.vy = sinf(a)*sp - 60.f;
        ep.r = color.r; ep.g = color.g; ep.b = color.b; ep.a = color.a;
        ep.maxLife = 1.f + dz(m_rng)*0.5f; ep.life = ep.maxLife;
        ep.size = 1.f + dz(m_rng); ep.flags = 0u; ep.orbitRadius = 0.f;
        m_emitQueue.push_back(ep);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Upload: read back staging, find dead slots, write new particles, upload
// ─────────────────────────────────────────────────────────────────────────────
void GPUParticleSystem::UploadEmitted(ID3D11Device*, ID3D11DeviceContext* ctx) {
    if (m_emitQueue.empty()) return;

    // Batch all new particles into a single upload call if they don't wrap around
    // For simplicity, we'll just handle them in chunks if they do wrap.
    size_t count = m_emitQueue.size();
    size_t start = m_nextSlot % MAX_PARTICLES;
    
    std::vector<GPUParticle> uploadData;
    uploadData.reserve(count);
    for (const auto& ep : m_emitQueue) {
        GPUParticle gp{};
        gp.x = ep.x; gp.y = ep.y; gp.vx = ep.vx; gp.vy = ep.vy;
        gp.r = ep.r; gp.g = ep.g; gp.b = ep.b; gp.a = ep.a;
        gp.life = ep.life; gp.maxLife = ep.maxLife;
        gp.size = ep.size; gp.flags = ep.flags;
        gp.anchorX = ep.anchorX;
        gp.orbitSpeed = ep.orbitSpeed;
        gp.orbitRadius = ep.orbitRadius;
        gp.orbitPhase = ep.orbitPhase;
        uploadData.push_back(gp);
    }

    if (start + count <= MAX_PARTICLES) {
        D3D11_BOX box{ (UINT)start * sizeof(GPUParticle), 0, 0, (UINT)(start + count) * sizeof(GPUParticle), 1, 1 };
        ctx->UpdateSubresource(m_particleBuffer.Get(), 0, &box, uploadData.data(), 0, 0);
    } else {
        // Wraps around: two calls
        size_t firstPart = MAX_PARTICLES - start;
        D3D11_BOX box1{ (UINT)start * sizeof(GPUParticle), 0, 0, MAX_PARTICLES * sizeof(GPUParticle), 1, 1 };
        ctx->UpdateSubresource(m_particleBuffer.Get(), 0, &box1, uploadData.data(), 0, 0);
        
        size_t secondPart = count - firstPart;
        D3D11_BOX box2{ 0, 0, 0, (UINT)secondPart * sizeof(GPUParticle), 1, 1 };
        ctx->UpdateSubresource(m_particleBuffer.Get(), 0, &box2, uploadData.data() + firstPart, 0, 0);
    }

    m_nextSlot += (uint32_t)count;
    if (m_activeSlots < MAX_PARTICLES)
        m_activeSlots = std::min(m_activeSlots + (uint32_t)count, MAX_PARTICLES);
    m_emitQueue.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Update: upload emits, dispatch compute shader
// ─────────────────────────────────────────────────────────────────────────────
void GPUParticleSystem::Update(ID3D11Device* device, ID3D11DeviceContext* ctx, float dt) {
    m_frameEmitCount = 0; // Reset for new frame
    // 1. Upload queued particles
    UploadEmitted(device, ctx);

    // 2. Update compute CB
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cbUpdate.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            static auto startTime = std::chrono::steady_clock::now();
            float* f = (float*)m.pData;
            f[0] = dt;
            f[1] = m_gravity;
            f[2] = (float)std::chrono::duration<double>(
                       std::chrono::steady_clock::now() - startTime).count();
            f[3] = m_emberMode ? 1.f : 0.f;
            ((uint32_t*)m.pData)[4] = MAX_PARTICLES;
            ctx->Unmap(m_cbUpdate.Get(), 0);
        }
    }

    // 3. Dispatch compute shader
    ctx->CSSetShader(m_csUpdate.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, m_cbUpdate.GetAddressOf());
    ID3D11UnorderedAccessView* uav = m_particleUAV.Get();
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);

    ctx->Dispatch((MAX_PARTICLES + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, 1, 1);

    // Unbind
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
    ctx->CSSetShader(nullptr, nullptr, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw: instanced quad draw, VS reads from StructuredBuffer via SV_InstanceID
// ─────────────────────────────────────────────────────────────────────────────
void GPUParticleSystem::Draw(ID3D11DeviceContext* ctx, SpriteBatch& batch) {
    // Flush any pending SpriteBatch draws
    batch.Flush();

    // Get viewport for draw CB
    D3D11_VIEWPORT vp;
    UINT numVP = 1;
    ctx->RSGetViewports(&numVP, &vp);

    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cbDraw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float* f = (float*)m.pData;
            f[0] = vp.Width; f[1] = vp.Height; f[2] = 0; f[3] = 0;
            ctx->Unmap(m_cbDraw.Get(), 0);
        }
    }

    // Set pipeline
    ctx->IASetInputLayout(m_drawLayout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = 16, offset = 0;
    ctx->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

    ctx->VSSetShader(m_vsDraw.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srv = m_particleSRV.Get();
    ctx->VSSetShaderResources(0, 1, &srv);
    ctx->VSSetConstantBuffers(0, 1, m_cbDraw.GetAddressOf());

    ctx->PSSetShader(m_psDraw.Get(), nullptr, 0);
    if (m_texture) ctx->PSSetShaderResources(0, 1, &m_texture);
    ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

    float bf[4] = {1,1,1,1};
    ctx->OMSetBlendState(m_additiveBlend.Get(), bf, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    ctx->RSSetState(m_rasterizer.Get());

    // Draw all MAX_PARTICLES as instances — dead ones produce degenerate quads
    ctx->DrawIndexedInstanced(6, MAX_PARTICLES, 0, 0, 0);

    // Unbind VS SRV
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
    ctx->VSSetShader(nullptr, nullptr, 0);
    ctx->PSSetShader(nullptr, nullptr, 0);
}

void GPUParticleSystem::Clear(ID3D11DeviceContext* ctx) {
    // Zero via staging
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(m_stagingBuffer.Get(), 0, D3D11_MAP_WRITE, 0, &m))) {
        memset(m.pData, 0, MAX_PARTICLES * sizeof(GPUParticle));
        ctx->Unmap(m_stagingBuffer.Get(), 0);
        ctx->CopyResource(m_particleBuffer.Get(), m_stagingBuffer.Get());
    }
    m_activeSlots = 0;
    m_emitQueue.clear();
}

} // namespace pfd
