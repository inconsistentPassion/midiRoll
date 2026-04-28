#include "Bloom.h"
#include <d3dcompiler.h>
#include <cstring>

namespace pfd {

// Inline HLSL for bloom passes
static const char* g_fullscreenVS = R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
VSOut VSMain(uint id : SV_VERTEXID) {
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2 - 1, 0, 1);
    o.pos.y = -o.pos.y;
    return o;
}
)";

static const char* g_brightPS = R"(
Texture2D    tex : register(t0);
SamplerState sam : register(s0);
cbuffer CB : register(b0) { float threshold; float intensity; float2 pad; };
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 c = tex.Sample(sam, uv);
    float brightness = dot(c.rgb, float3(0.2126, 0.7152, 0.0722));
    return (brightness > threshold) ? c * intensity : float4(0,0,0,0);
}
)";

static const char* g_blurHPS = R"(
Texture2D    tex : register(t0);
SamplerState sam : register(s0);
cbuffer CB : register(b0) { float texelSize; float3 pad2; };
static const float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 result = tex.Sample(sam, uv) * weights[0];
    for (int i = 1; i < 5; i++) {
        float offset = texelSize * i;
        result += tex.Sample(sam, uv + float2(offset, 0)) * weights[i];
        result += tex.Sample(sam, uv - float2(offset, 0)) * weights[i];
    }
    return result;
}
)";

static const char* g_blurVPS = R"(
Texture2D    tex : register(t0);
SamplerState sam : register(s0);
cbuffer CB : register(b0) { float texelSize; float3 pad2; };
static const float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 result = tex.Sample(sam, uv) * weights[0];
    for (int i = 1; i < 5; i++) {
        float offset = texelSize * i;
        result += tex.Sample(sam, uv + float2(0, offset)) * weights[i];
        result += tex.Sample(sam, uv - float2(0, offset)) * weights[i];
    }
    return result;
}
)";

static const char* g_compositePS = R"(
Texture2D sceneTex : register(t0);
Texture2D bloomTex : register(t1);
SamplerState sam    : register(s0);
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float4 scene = sceneTex.Sample(sam, uv);
    float4 bloom = bloomTex.Sample(sam, uv);
    return scene + bloom;
}
)";

static ComPtr<ID3DBlob> CompileVS(const char* src) {
    ComPtr<ID3DBlob> blob, err;
    D3DCompile(src, strlen(src), "vs", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0,
               blob.GetAddressOf(), err.GetAddressOf());
    return blob;
}

static ComPtr<ID3DBlob> CompilePS(const char* src, const char* entry = "PSMain") {
    ComPtr<ID3DBlob> blob, err;
    D3DCompile(src, strlen(src), "ps", nullptr, nullptr, entry, "ps_5_0", 0, 0,
               blob.GetAddressOf(), err.GetAddressOf());
    return blob;
}

bool Bloom::Initialize(ID3D11Device* device, uint32_t width, uint32_t height) {
    m_brightPass.Create(device, width / 2, height / 2);
    m_blurH.Create(device, width / 2, height / 2);
    m_blurV.Create(device, width / 2, height / 2);

    auto vsBlob = CompileVS(g_fullscreenVS);
    device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                               nullptr, m_fullscreenVS.GetAddressOf());

    auto psBright = CompilePS(g_brightPS);
    device->CreatePixelShader(psBright->GetBufferPointer(), psBright->GetBufferSize(),
                              nullptr, m_brightPS.GetAddressOf());

    auto psBlurH = CompilePS(g_blurHPS);
    device->CreatePixelShader(psBlurH->GetBufferPointer(), psBlurH->GetBufferSize(),
                              nullptr, m_blurHPS.GetAddressOf());

    auto psBlurV = CompilePS(g_blurVPS);
    device->CreatePixelShader(psBlurV->GetBufferPointer(), psBlurV->GetBufferSize(),
                              nullptr, m_blurVPS.GetAddressOf());

    auto psComposite = CompilePS(g_compositePS);
    device->CreatePixelShader(psComposite->GetBufferPointer(), psComposite->GetBufferSize(),
                              nullptr, m_compositePS.GetAddressOf());

    // Sampler
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, m_sampler.GetAddressOf());

    // Constant buffer for blur params
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    device->CreateBuffer(&bd, nullptr, m_cbBlur.GetAddressOf());

    return true;
}

void Bloom::Resize(ID3D11Device* device, uint32_t width, uint32_t height) {
    m_brightPass.Create(device, width / 2, height / 2);
    m_blurH.Create(device, width / 2, height / 2);
    m_blurV.Create(device, width / 2, height / 2);
}

void Bloom::Apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* scene,
                   ID3D11RenderTargetView* output, uint32_t viewW, uint32_t viewH) {
    // 1. Bright pass
    m_brightPass.Bind(ctx);
    m_brightPass.Clear(ctx, 0, 0, 0, 0);
    ctx->PSSetShader(m_brightPS.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &scene);
    ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    // Update threshold/intensity CB here if needed
    ctx->Draw(3, 0);

    // 2. Horizontal blur
    m_blurH.Bind(ctx);
    m_blurH.Clear(ctx, 0, 0, 0, 0);
    ctx->PSSetShader(m_blurHPS.Get(), nullptr, 0);
    auto srv = m_brightPass.SRV();
    ctx->PSSetShaderResources(0, 1, &srv);
    float texelW = 1.0f / (float)(viewW / 2);
    D3D11_MAPPED_SUBRESOURCE mapped;
    ctx->Map(m_cbBlur.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &texelW, 4);
    ctx->Unmap(m_cbBlur.Get(), 0);
    ctx->PSSetConstantBuffers(0, 1, m_cbBlur.GetAddressOf());
    ctx->Draw(3, 0);

    // 3. Vertical blur
    m_blurV.Bind(ctx);
    m_blurV.Clear(ctx, 0, 0, 0, 0);
    ctx->PSSetShader(m_blurVPS.Get(), nullptr, 0);
    srv = m_blurH.SRV();
    ctx->PSSetShaderResources(0, 1, &srv);
    float texelH = 1.0f / (float)(viewH / 2);
    ctx->Map(m_cbBlur.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &texelH, 4);
    ctx->Unmap(m_cbBlur.Get(), 0);
    ctx->Draw(3, 0);

    // 4. Composite (scene + bloom)
    D3D11_VIEWPORT vp{};
    vp.Width = (float)viewW; vp.Height = (float)viewH; vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &output, nullptr);
    ctx->PSSetShader(m_compositePS.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {scene, m_blurV.SRV()};
    ctx->PSSetShaderResources(0, 2, srvs);
    ctx->Draw(3, 0);

    // Cleanup
    ID3D11ShaderResourceView* null[2] = {};
    ctx->PSSetShaderResources(0, 2, null);
}

} // namespace pfd
