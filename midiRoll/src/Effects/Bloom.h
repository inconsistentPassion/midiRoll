#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/RenderTarget.h"

namespace pfd {

// Multi-pass gaussian bloom post-processing
class Bloom {
public:
    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);
    void Resize(ID3D11Device* device, uint32_t width, uint32_t height);
    void Apply(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* scene,
               ID3D11RenderTargetView* output, uint32_t viewW, uint32_t viewH);

    void SetIntensity(float i) { m_intensity = i; }
    void SetThreshold(float t) { m_threshold = t; }

private:
    RenderTarget m_brightPass;
    RenderTarget m_blurH;
    RenderTarget m_blurV;

    ComPtr<ID3D11VertexShader> m_fullscreenVS;
    ComPtr<ID3D11PixelShader>  m_brightPS;
    ComPtr<ID3D11PixelShader>  m_blurHPS;
    ComPtr<ID3D11PixelShader>  m_blurVPS;
    ComPtr<ID3D11PixelShader>  m_compositePS;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Buffer>       m_cbBlur;

    float m_intensity = 0.8f;
    float m_threshold = 0.6f;
};

} // namespace pfd
