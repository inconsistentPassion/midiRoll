#include "GlassRenderer.h"
#include <d3dcompiler.h>
#include <cmath>
#include <cstring>

namespace pfd {

// ═════════════════════════════════════════════════════════════════════════════
// Blob shader — soft colored circles with smooth falloff
// ═════════════════════════════════════════════════════════════════════════════

static const char* g_blobVS = R"(
cbuffer CBBlob : register(b0) {
    float viewWidth;
    float viewHeight;
    float time;
    float pad0;
};

struct VSIn {
    float2 quadPos : POSITION;  // 0..1 unit quad
    float2 instCenter : TEXCOORD0; // normalized screen pos
    float  instRadius : TEXCOORD1; // normalized radius
    float4 instColor  : COLOR0;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input) {
    VSOut o;
    // Expand quad to cover the blob area in screen space
    float2 worldPos = input.instCenter + (input.quadPos - 0.5) * 2.0 * input.instRadius;

    o.pos.x = worldPos.x * 2.0 - 1.0;
    o.pos.y = 1.0 - worldPos.y * 2.0;
    o.pos.z = 0.0;
    o.pos.w = 1.0;
    o.uv = input.quadPos;
    o.color = input.instColor;
    return o;
}
)";

static const char* g_blobPS = R"(
struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

float4 PSMain(PSIn input) : SV_TARGET {
    float d = distance(input.uv, float2(0.5, 0.5));
    // Soft falloff: fully opaque at center, smooth fade to edge
    float alpha = smoothstep(0.5, 0.15, d) * input.color.a;
    return float4(input.color.rgb, alpha);
}
)";

// ═════════════════════════════════════════════════════════════════════════════
// Blob instance data
// ═════════════════════════════════════════════════════════════════════════════

struct BlobInstance {
    float centerX, centerY; // normalized screen position
    float radius;           // normalized radius (fraction of screen)
    float r, g, b, a;       // color
};

struct CBBlob {
    float viewWidth;
    float viewHeight;
    float time;
    float pad;
};

// ═════════════════════════════════════════════════════════════════════════════
// Initialization
// ═════════════════════════════════════════════════════════════════════════════

bool GlassRenderer::Initialize(ID3D11Device* device) {
    if (!CreateShaders(device)) return false;
    if (!CreateStates(device)) return false;

    // Create dynamic vertex buffer for blob instances
    // Each blob = 6 vertices (2 triangles) with per-vertex data:
    //   float2 quadPos + float2 center + float radius + float4 color = 9 floats
    // But we use instancing: 6 vertex floats + instance data
    // Simpler: just use a dynamic VB with enough room for all blobs
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 6 * 9 * sizeof(float) * glass::BLOB_COUNT; // room for all blobs
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&bd, nullptr, m_vb.GetAddressOf());
    if (FAILED(hr)) return false;

    // Constant buffer
    bd.ByteWidth = sizeof(CBBlob);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_cb.GetAddressOf());
    return SUCCEEDED(hr);
}

void GlassRenderer::Shutdown() {
    // ComPtr handles cleanup
}

// ═════════════════════════════════════════════════════════════════════════════
// Shader compilation
// ═════════════════════════════════════════════════════════════════════════════

bool GlassRenderer::CreateShaders(ID3D11Device* device) {
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    HRESULT hr;

    hr = D3DCompile(g_blobVS, strlen(g_blobVS), "blobVS", nullptr, nullptr,
                    "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(g_blobPS, strlen(g_blobPS), "blobPS", nullptr, nullptr,
                    "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                     nullptr, m_vs.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                    nullptr, m_ps.GetAddressOf());
    if (FAILED(hr)) return false;

    // Input layout: per-vertex quadPos + per-instance data
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,          1, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT,  1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

    hr = device->CreateInputLayout(layout, _countof(layout),
                                    vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                    m_layout.GetAddressOf());
    return SUCCEEDED(hr);
}

bool GlassRenderer::CreateBlobTexture(ID3D11Device* device) {
    // Not needed — the shader computes soft circles analytically
    return true;
}

bool GlassRenderer::CreateStates(ID3D11Device* device) {
    // Additive blend for blobs (they layer on top of each other)
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device->CreateBlendState(&bd, m_blendState.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(&rs, m_rasterizer.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    hr = device->CreateDepthStencilState(&ds, m_depthStencil.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = device->CreateSamplerState(&sd, m_sampler.GetAddressOf());
    return SUCCEEDED(hr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Render — draw all background blobs
// ═════════════════════════════════════════════════════════════════════════════
//
// Each blob is rendered as a quad with 6 vertices (2 triangles).
// The vertex data is packed as: quadPos(2) + center(2) + radius(1) + color(4) = 9 floats
// We use instancing: the quad is the same for all blobs, instance data varies.
//
// But since we only have ~6 blobs, it's simpler to just expand into a single
// vertex buffer with all the triangles pre-built. No instancing overhead.
//
// Layout per vertex: float2 pos, float2 center, float radius, float4 color
// Per blob: 6 vertices (2 triangles)

void GlassRenderer::Render(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH, float time) {
    if (!m_vb || !m_vs) return;

    auto* blobs = glass::GetDefaultBlobs();

    // Build vertex data for all blobs
    // Per vertex: float2 quadPos, float2 center, float radius, float4 color = 9 floats
    struct Vert {
        float qx, qy;   // quad position (0..1)
        float cx, cy;    // blob center (normalized)
        float radius;    // blob radius (normalized)
        float r, g, b, a;
    };

    Vert verts[glass::BLOB_COUNT * 6];

    for (int i = 0; i < glass::BLOB_COUNT; i++) {
        auto& blob = blobs[i];
        float cx, cy;
        blob.GetAnimatedPos(time, cx, cy);

        // Quad corners (two triangles)
        float x0 = cx - blob.radius;
        float y0 = cy - blob.radius;
        float x1 = cx + blob.radius;
        float y1 = cy + blob.radius;

        // Triangle 1: (0,0) (1,0) (1,1)
        // Triangle 2: (0,0) (1,1) (0,1)
        // But quadPos is in 0..1 range, center and radius are in normalized screen coords
        auto setVert = [&](int idx, float qx, float qy) {
            verts[i * 6 + idx] = {qx, qy, cx, cy, blob.radius,
                                   blob.color[0], blob.color[1], blob.color[2], blob.color[3]};
        };
        setVert(0, 0, 0);
        setVert(1, 1, 0);
        setVert(2, 1, 1);
        setVert(3, 0, 0);
        setVert(4, 1, 1);
        setVert(5, 0, 1);
    }

    // Upload
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, verts, sizeof(verts));
    ctx->Unmap(m_vb.Get(), 0);

    // Update constant buffer
    CBBlob cb{(float)viewW, (float)viewH, time, 0};
    hr = ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, &cb, sizeof(cb));
    ctx->Unmap(m_cb.Get(), 0);

    // Bind pipeline
    UINT stride = sizeof(Vert);
    UINT offset = 0;
    ctx->IASetInputLayout(m_layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);

    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

    ctx->PSSetShader(m_ps.Get(), nullptr, 0);

    float bf[4] = {1,1,1,1};
    ctx->OMSetBlendState(m_blendState.Get(), bf, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    ctx->RSSetState(m_rasterizer.Get());

    ctx->Draw(glass::BLOB_COUNT * 6, 0);
}

} // namespace pfd
