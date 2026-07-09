#pragma once
// ═════════════════════════════════════════════════════════════════════════════
// GlassRenderer — Background blob system for Frosted Glass UI
//
// Glass needs color to refract. A glass panel over solid black is invisible.
// The background must have colored elements for the glass to show its effect.
//
// This renders 4-6 large, soft, slowly-moving colored circles behind the UI
// layer. They are NOT part of the game world — they exist purely to give the
// glass something to refract.
//
// Each blob is a pre-blurred circle texture (one draw call per blob).
// Do NOT real-time blur the blobs — use a pre-blurred circle texture scaled
// to the right size. This is cheap.
//
// Animation: slow sine/cosine oscillation, 20-30s per cycle.
// Each blob has different speed and phase.
// ═════════════════════════════════════════════════════════════════════════════

#include "D3DContext.h"
#include "FrostedGlassTheme.h"
#include "../Util/Math.h"
#include <cstdint>

namespace pfd {

class GlassRenderer {
public:
    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // Render all background blobs. Call BEFORE drawing glass UI elements.
    // time: running clock in seconds (drives blob animation)
    void Render(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH, float time);

private:
    // Blob vertex shader
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
    ComPtr<ID3D11InputLayout>  m_layout;
    ComPtr<ID3D11Buffer>       m_vb;       // dynamic vertex buffer for blob quads
    ComPtr<ID3D11Buffer>       m_cb;
    ComPtr<ID3D11BlendState>   m_blendState;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11DepthStencilState> m_depthStencil;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Pre-blurred circle texture (generated once at init)
    ComPtr<ID3D11Texture2D>          m_blobTex;
    ComPtr<ID3D11ShaderResourceView> m_blobSRV;

    bool CreateShaders(ID3D11Device* device);
    bool CreateBlobTexture(ID3D11Device* device);
    bool CreateStates(ID3D11Device* device);
};

} // namespace pfd
