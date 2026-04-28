#include "RenderTarget.h"

namespace pfd {

bool RenderTarget::Create(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format) {
    m_width = width;
    m_height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width      = width;
    desc.Height     = height;
    desc.MipLevels  = 1;
    desc.ArraySize  = 1;
    desc.Format     = format;
    desc.SampleDesc.Count = 1;
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, m_texture.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreateRenderTargetView(m_texture.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                    = format;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;
    hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc, m_srv.GetAddressOf());
    return SUCCEEDED(hr);
}

void RenderTarget::Clear(ID3D11DeviceContext* ctx, float r, float g, float b, float a) {
    float color[] = {r, g, b, a};
    ctx->ClearRenderTargetView(m_rtv.Get(), color);
}

void RenderTarget::Bind(ID3D11DeviceContext* ctx) {
    D3D11_VIEWPORT vp{};
    vp.Width    = (float)m_width;
    vp.Height   = (float)m_height;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
}

void RenderTarget::Unbind(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* restoreRTV) {
    ctx->OMSetRenderTargets(1, &restoreRTV, nullptr);
}

} // namespace pfd
