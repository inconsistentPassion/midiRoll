#include "D3DContext.h"
#include <stdexcept>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace pfd {

bool D3DContext::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount                        = 2;
    desc.BufferDesc.Width                   = width;
    desc.BufferDesc.Height                  = height;
    desc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator   = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow                       = hwnd;
    desc.SampleDesc.Count                   = 1;
    desc.Windowed                           = TRUE;
    desc.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, levels, _countof(levels),
        D3D11_SDK_VERSION,
        &desc, m_swapChain.GetAddressOf(),
        m_device.GetAddressOf(), &m_featureLevel,
        m_context.GetAddressOf()
    );

    if (FAILED(hr)) return false;

    // Back buffer RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());

    CreateDepthStencil(width, height);

    // Bind
    m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencil.Get());

    // Viewport
    D3D11_VIEWPORT vp{};
    vp.Width    = (float)width;
    vp.Height   = (float)height;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

void D3DContext::Resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    m_width = width;
    m_height = height;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferRTV.Reset();
    m_depthStencil.Reset();
    m_depthStencilTex.Reset();

    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());

    CreateDepthStencil(width, height);
    m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencil.Get());

    D3D11_VIEWPORT vp{};
    vp.Width    = (float)width;
    vp.Height   = (float)height;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

void D3DContext::Present(bool vsync) {
    m_swapChain->Present(vsync ? 1 : 0, 0);
}

void D3DContext::Clear(float r, float g, float b, float a) {
    float color[] = {r, g, b, a};
    m_context->ClearRenderTargetView(m_backBufferRTV.Get(), color);
    m_context->ClearDepthStencilView(m_depthStencil.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void D3DContext::CreateDepthStencil(uint32_t w, uint32_t h) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width      = w;
    desc.Height     = h;
    desc.MipLevels  = 1;
    desc.ArraySize  = 1;
    desc.Format     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = D3D11_BIND_DEPTH_STENCIL;

    m_device->CreateTexture2D(&desc, nullptr, m_depthStencilTex.GetAddressOf());
    m_device->CreateDepthStencilView(m_depthStencilTex.Get(), nullptr, m_depthStencil.GetAddressOf());
}

} // namespace pfd
