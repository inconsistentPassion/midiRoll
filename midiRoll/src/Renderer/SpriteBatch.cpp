#include "SpriteBatch.h"
#include <d3dcompiler.h>

namespace pfd {

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
    float2 pixelPos = input.pos * input.instSize + input.instPos;
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
    float4 texColor = tex.Sample(sam, input.uv);
    // Correctly handle both RGBA and R8 textures
    // For R8, Sample() returns (R, 0, 0, 1), so we use R as alpha.
    return float4(input.color.rgb, input.color.a * texColor.r);
}
)";

bool SpriteBatch::Initialize(ID3D11Device* device) {
    m_instances.reserve(m_maxInstances);
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    D3DCompile(g_spriteVS, strlen(g_spriteVS), "spriteVS", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf());
    D3DCompile(g_spritePS, strlen(g_spritePS), "spritePS", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), err.GetAddressOf());
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.GetAddressOf());
    device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.GetAddressOf());

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
    device->CreateInputLayout(layout, _countof(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_layout.GetAddressOf());

    SpriteVertex quadVerts[] = { {{0,0},{0,0},{1,1,1,1}}, {{1,0},{1,0},{1,1,1,1}}, {{1,1},{1,1},{1,1,1,1}}, {{0,1},{0,1},{1,1,1,1}} };
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(quadVerts);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = quadVerts;
    device->CreateBuffer(&bd, &init, m_vertexBuffer.GetAddressOf());

    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = indices;
    device->CreateBuffer(&bd, &init, m_indexBuffer.GetAddressOf());

    bd.ByteWidth = (UINT)(m_maxInstances * sizeof(SpriteInstance));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, m_instanceBuffer.GetAddressOf());

    bd.ByteWidth = sizeof(CBPerFrame);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, m_cbPerFrame.GetAddressOf());

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());

    // Additive Blend State
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    device->CreateBlendState(&blendDesc, m_additiveBlendState.GetAddressOf());

    D3D11_RASTERIZER_DESC rsDesc{};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rsDesc, m_rasterizerState.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = FALSE;
    device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf());

    // Sampler state
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD         = 0;
    sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf());

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

void SpriteBatch::Begin(ID3D11DeviceContext* ctx, uint32_t viewWidth, uint32_t viewHeight) {
    m_ctx = ctx;
    m_viewW = viewWidth;
    m_viewH = viewHeight;
    m_instances.clear(); // capacity is retained; no reserve needed
    m_currentTex = m_whiteTexture.Get();
    m_additive = false;
}

void SpriteBatch::Draw(util::Vec2 pos, util::Vec2 size, util::Vec4 color, util::Vec2 uvOrigin, util::Vec2 uvSize) {
    Draw(pos, size, m_currentTex, uvOrigin, {uvOrigin.x + uvSize.x, uvOrigin.y + uvSize.y}, color);
}

void SpriteBatch::Draw(util::Vec2 pos, util::Vec2 size, ID3D11ShaderResourceView* srv, util::Vec2 uvOrigin, util::Vec2 uvEnd, util::Vec4 color) {
    ID3D11ShaderResourceView* tex = srv ? srv : m_whiteTexture.Get();
    if (m_currentTex != tex) {
        Flush();
        m_currentTex = tex;
    }
    if (m_instances.size() >= m_maxInstances) Flush();
    m_instances.push_back({pos, size, color, uvOrigin, {uvEnd.x - uvOrigin.x, uvEnd.y - uvOrigin.y}});
}

void SpriteBatch::DrawGradientV(util::Vec2 pos, util::Vec2 size, util::Vec4 topColor, util::Vec4 bottomColor) {
    float halfH = size.y * 0.5f;
    Draw({pos.x, pos.y}, {size.x, halfH}, topColor);
    Draw({pos.x, pos.y + halfH}, {size.x, halfH}, bottomColor);
}

void SpriteBatch::End() {
    Flush();
    m_ctx = nullptr;
}

void SpriteBatch::Flush() {
    if (m_instances.empty() || !m_ctx) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_ctx->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, m_instances.data(), m_instances.size() * sizeof(SpriteInstance));
    m_ctx->Unmap(m_instanceBuffer.Get(), 0);

    CBPerFrame cb{(float)m_viewW, (float)m_viewH, 0, 0};
    m_ctx->Map(m_cbPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    m_ctx->Unmap(m_cbPerFrame.Get(), 0);

    m_ctx->IASetInputLayout(m_layout.Get());
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT strides[2] = {sizeof(SpriteVertex), sizeof(SpriteInstance)}, offsets[2] = {0, 0};
    ID3D11Buffer* bufs[2] = {m_vertexBuffer.Get(), m_instanceBuffer.Get()};
    m_ctx->IASetVertexBuffers(0, 2, bufs, strides, offsets);
    m_ctx->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, m_cbPerFrame.GetAddressOf());
    m_ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, &m_currentTex);
    m_ctx->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());
    float bf[4] = {1,1,1,1};
    m_ctx->OMSetBlendState(m_additive ? m_additiveBlendState.Get() : m_blendState.Get(), bf, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    m_ctx->RSSetState(m_rasterizerState.Get());
    m_ctx->DrawIndexedInstanced(6, (UINT)m_instances.size(), 0, 0, 0);
    m_instances.clear();
}

void SpriteBatch::SetTexture(ID3D11ShaderResourceView* srv) {
    ID3D11ShaderResourceView* tex = srv ? srv : m_whiteTexture.Get();
    if (m_currentTex != tex) {
        Flush();
        m_currentTex = tex;
    }
}

void SpriteBatch::SetBlendMode(bool additive) {
    if (m_additive != additive) {
        Flush();
        m_additive = additive;
    }
}

} // namespace pfd
