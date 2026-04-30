#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "../Util/Color.h"
#include <vector>
#include <random>
#include <cstdint>

namespace pfd {

// GPU-friendly particle struct (must match HLSL layout exactly)
struct alignas(16) GPUParticle {
    float x, y, vx, vy;           // offset 0:  position + velocity
    float r, g, b, a;              // offset 16: color
    float life, maxLife, size;     // offset 32: lifetime + size
    uint32_t flags;                // offset 44: bit0=isGlow, bit1=isSmoke
    float anchorX, orbitSpeed;     // offset 48: orbit params
    float orbitRadius, orbitPhase; // offset 56: orbit params
};
static_assert(sizeof(GPUParticle) == 64, "GPUParticle must be 64 bytes for GPU alignment");

// CPU-side particle for emit queuing (same layout, just not aligned)
struct EmitParticle {
    float x, y, vx, vy;
    float r, g, b, a;
    float life, maxLife, size;
    uint32_t flags;
    float anchorX, orbitSpeed;
    float orbitRadius, orbitPhase;
};

class GPUParticleSystem {
public:
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // CPU-side emit (queues particles for next GPU upload)
    void EmitBurst(float x, float y, util::Color color, int count = 20);
    void EmitContinuous(float centerX, float y, float noteW, util::Color color);
    void EmitSparks(float x, float y, util::Color color);

    // GPU dispatch + render
    void Update(ID3D11Device* ctx_device, ID3D11DeviceContext* ctx, float dt);
    void Draw(ID3D11DeviceContext* ctx, SpriteBatch& batch);

    void SetTexture(ID3D11ShaderResourceView* tex) { m_texture = tex; }
    void SetGravity(float g) { m_gravity = g; }
    void SetEmberMode(bool on) { m_emberMode = on; }
    size_t Count() const { return m_activeSlots; }
    void Clear(ID3D11DeviceContext* ctx);

    static constexpr uint32_t MAX_PARTICLES = 50000;
    static constexpr uint32_t THREAD_GROUP_SIZE = 256;

private:
    void CreateBuffers(ID3D11Device* device);
    void CreateShaders(ID3D11Device* device);
    void UploadEmitted(ID3D11Device* device, ID3D11DeviceContext* ctx);

    // ── GPU resources ──
    // Main particle buffer (GPU read/write)
    ComPtr<ID3D11Buffer>              m_particleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_particleSRV;

    // Staging buffer (same size, CPU-readable for finding dead slots)
    ComPtr<ID3D11Buffer>              m_stagingBuffer;

    // Constant buffers
    ComPtr<ID3D11Buffer>              m_cbUpdate;    // compute shader CB
    ComPtr<ID3D11Buffer>              m_cbDraw;      // draw VS CB

    // Compute shader
    ComPtr<ID3D11ComputeShader>       m_csUpdate;

    // Draw shaders (particle-specific, reads from StructuredBuffer)
    ComPtr<ID3D11VertexShader>        m_vsDraw;
    ComPtr<ID3D11PixelShader>         m_psDraw;
    ComPtr<ID3D11InputLayout>         m_drawLayout;

    // Draw resources
    ComPtr<ID3D11Buffer>              m_quadVB;
    ComPtr<ID3D11Buffer>              m_quadIB;
    ComPtr<ID3D11BlendState>          m_additiveBlend;
    ComPtr<ID3D11RasterizerState>     m_rasterizer;
    ComPtr<ID3D11DepthStencilState>   m_depthStencil;
    ComPtr<ID3D11SamplerState>        m_sampler;

    // ── CPU state ──
    std::vector<EmitParticle>         m_emitQueue;
    std::mt19937                      m_rng{std::random_device{}()};

    ID3D11ShaderResourceView*         m_texture = nullptr;
    float                             m_gravity = 0.0f;
    bool                              m_emberMode = false;
    uint32_t                          m_activeSlots = 0;
    uint32_t                          m_nextSlot = 0;
    uint32_t                          m_frameEmitCount = 0;
    static const uint32_t             MAX_FRAME_EMIT = 4000;
    bool                              m_initialized = false;
};

} // namespace pfd
