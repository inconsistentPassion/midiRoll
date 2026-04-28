#include "SpriteBatch.h"
#include <d3dcompiler.h>

namespace pfd {

// ---- Inline HLSL shaders for SpriteBatch ----
static const char* g_spriteVS = R"(
cbuffer CBPerFrame : register(b0) {
    float viewWidth;
    float viewHeight;
    float2 pad;
};

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    // Per-instance
    float2 instPos    : INST_POS;
    float2 instSize   : INST_SIZE;
    float4 instColor  : INST_COLOR;
    float2 instUVOrig : INST_UVORIG;
    float2 instUVSize : INST_UVSIZE;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input) {
    // Scale quad corner by instance size, offset by instance position
    float2 pixelPos = input.pos * input.instSize + input.instPos;

    // Convert pixels to NDC [-1, 1], Y-down
    float2 ndc;
    ndc.x = (pixelPos.x / viewWidth) * 2.0 - 1.0;
    ndc.y = 1.0 - (pixelPos.y / viewHeight) * 2.0;

    VSOut o;
    o.pos   = float4(ndc, 0, 1);
    o.uv    = input.uv * input.instUVSize + input.instUVOrig;
    o.color = input.color * input.instColor;
    return o;
}
)";

static const char* g_spritePS = R"(
Texture2D    tex : register(t0);
SamplerState sam : register(s0);

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

float4 PSMain(PSIn input) : SV_TARGET {
    return tex.Sample(sam, input.uv) * input.color;
}
)";

bool SpriteBatch::Initialize(ID3D11Device* device) {
    m_instances.reserve(m_maxInstances);

    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    D3DCompile(g_spriteVS, strlen(g_spriteVS), "spriteVS", nullptr, nullptr,
               "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf());
    D3DCompile(g_spritePS, strlen(g_spritePS), "spritePS", nullptr, nullptr,
               "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), err.GetAddressOf());

    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                               nullptr, m_vs.GetAddressOf());
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                              nullptr, m_ps.GetAddressOf());

    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"INST_POS",     0, DXGI_FORMAT_R32G32_FLOAT,       1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_SIZE",    0, DXGI_FORMAT_R32G32_FLOAT,       1, 8,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_COLOR",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_UVORIG",  0, DXGI_FORMAT_R32G32_FLOAT,       1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INST_UVSIZE",  0, DXGI_FORMAT_R32G32_FLOAT,       1, 40, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    device->CreateInputLayout(layout, _countof(layout),
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_layout.GetAddressOf());

    // Vertex buffer for a unit quad (4 corners)
    SpriteVertex quadVerts[] = {
        {{0, 0}, {0, 0}, {1,1,1,1}},
        {{1, 0}, {1, 0}, {1,1,1,1}},
        {{1, 1}, {1, 1}, {1,1,1,1}},
        {{0, 1}, {0, 1}, {1,1,1,1}},
    };
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(quadVerts);
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = quadVerts;
    device->CreateBuffer(&bd, &init, m_vertexBuffer.GetAddressOf());

    // Index buffer
    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    bd.ByteWidth = sizeof(indices);
    init.pSysMem = indices;
    device->CreateBuffer(&bd, &init, m_indexBuffer.GetAddressOf());

    // Instance buffer (dynamic)
    bd.ByteWidth = (UINT)(m_maxInstances * sizeof(SpriteInstance));
    bd.Usage     = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, m_instanceBuffer.GetAddressOf());

    // Constant buffer
    bd.ByteWidth = sizeof(CBPerFrame);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    device->CreateBuffer(&bd, nullptr, m_cbPerFrame.GetAddressOf());

    // Blend state (alpha blending)
    D3D11_BLEND_DESC blendDesc{};
    auto& rt = blendDesc.RenderTarget[0];
    rt.BlendEnable    = TRUE;
    rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp        = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());

    // Rasterizer (no cull, no scissor)
    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rsDesc, m_rasterizerState.GetAddressOf());

    // Depth stencil (disabled for 2D)
    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = FALSE;
    device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());

    // 1x1 white texture
    uint32_t white = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    init.pSysMem = &white;
    init.SysMemPitch = 4;
    ComPtr<ID3D11Texture2D> whiteTex;
    device->CreateTexture2D(&texDesc, &init, whiteTex.GetAddressOf());
    device->CreateShaderResourceView(whiteTex.Get(), nullptr, m_whiteTexture.GetAddressOf());

    return true;
}

void SpriteBatch::Begin(ID3D11DeviceContext*) {
    m_instances.clear();
}

void SpriteBatch::Draw(util::Vec2 pos, util::Vec2 size, util::Vec4 color,
                        util::Vec2 uvOrigin, util::Vec2 uvSize) {
    if (m_instances.size() >= m_maxInstances) return;
    m_instances.push_back({pos, size, color, uvOrigin, uvSize});
}

void SpriteBatch::DrawGradientV(util::Vec2 pos, util::Vec2 size,
                                 util::Vec4 topColor, util::Vec4 bottomColor) {
    // Approximate vertical gradient with two quads
    float halfH = size.y * 0.5f;
    Draw({pos.x, pos.y},           {size.x, halfH}, topColor);
    Draw({pos.x, pos.y + halfH},   {size.x, halfH}, bottomColor);
}

void SpriteBatch::End(ID3D11DeviceContext* ctx, uint32_t viewWidth, uint32_t viewHeight) {
    if (m_instances.empty()) return;

    // Upload instances
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, m_instances.data(), m_instances.size() * sizeof(SpriteInstance));
    ctx->Unmap(m_instanceBuffer.Get(), 0);

    // Update constant buffer
    CBPerFrame cb{(float)viewWidth, (float)viewHeight, 0, 0};
    ctx->Map(m_cbPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    ctx->Unmap(m_cbPerFrame.Get(), 0);

    // Set state
    ctx->IASetInputLayout(m_layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT strides[2] = {sizeof(SpriteVertex), sizeof(SpriteInstance)};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer* bufs[2] = {m_vertexBuffer.Get(), m_instanceBuffer.Get()};
    ctx->IASetVertexBuffers(0, 2, bufs, strides, offsets);
    ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m_cbPerFrame.GetAddressOf());

    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, m_whiteTexture.GetAddressOf());

    float blendFactor[4] = {1,1,1,1};
    ctx->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    ctx->RSSetState(m_rasterizerState.Get());

    // Draw instanced
    ctx->DrawIndexedInstanced(6, (UINT)m_instances.size(), 0);
}

} // namespace pfd
