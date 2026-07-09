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
    // Disabled debug flag due to missing SDK layers on target system
    /*
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    */

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

    // Fallback if Debug Layer is missing
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            flags, levels, _countof(levels),
            D3D11_SDK_VERSION,
            &desc, m_swapChain.GetAddressOf(),
            m_device.GetAddressOf(), &m_featureLevel,
            m_context.GetAddressOf()
        );
    }

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
    // Note: width/height come from WM_SIZE which is logical; 
    // but we store physical pixels from Initialize. Skip DPI here 
    // since the caller already provides the right values after DPI fix.
    m_width = width;
    m_height = height;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferRTV.Reset();
    m_depthStencil.Reset();
    m_depthStencilTex.Reset();
    m_sceneCopyTex.Reset();
    m_sceneCopySRV.Reset();

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

ID3D11ShaderResourceView* D3DContext::CaptureScreen() {
    if (!m_sceneCopyTex) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        m_device->CreateTexture2D(&desc, nullptr, m_sceneCopyTex.GetAddressOf());
        m_device->CreateShaderResourceView(m_sceneCopyTex.Get(), nullptr, m_sceneCopySRV.GetAddressOf());
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_context->CopyResource(m_sceneCopyTex.Get(), backBuffer.Get());
    return m_sceneCopySRV.Get();
}

// ═════════════════════════════════════════════════════════════════════════════
// Frosted Glass: mipmap scene capture
//
// The glass blur effect uses mipmap-stacking instead of multi-pass
// Gaussian/Kawase. Every mipmap level is already a blurred version of the
// original. The GPU generates mipmaps automatically — each level is the
// previous level averaged down by 2x. The glass shader samples at the
// appropriate level via SampleLevel(), which maps to textureLod() in GLSL.
//
// Cost: ~0.05ms per frame for GenerateMips. All glass elements share ONE
// mipmap generation per frame.
// ═════════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* D3DContext::CaptureSceneForGlass() {
    // Calculate required mip levels
    int maxDim = (int)std::max(m_width, m_height);
    int mipLevels = 1;
    int dim = maxDim;
    while (dim > 1) { dim /= 2; mipLevels++; }

    // Recreate texture if size or mip count changed
    bool needsRecreate = !m_glassSceneTex;
    if (!needsRecreate) {
        D3D11_TEXTURE2D_DESC existing;
        m_glassSceneTex->GetDesc(&existing);
        if (existing.Width != m_width || existing.Height != m_height ||
            existing.MipLevels != (UINT)mipLevels) {
            needsRecreate = true;
        }
    }

    if (needsRecreate) {
        m_glassSceneTex.Reset();
        m_glassSceneSRV.Reset();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = (UINT)mipLevels;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_glassSceneTex.GetAddressOf());
        if (FAILED(hr)) return nullptr;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = (UINT)mipLevels;
        hr = m_device->CreateShaderResourceView(m_glassSceneTex.Get(), &srvDesc,
                                                m_glassSceneSRV.GetAddressOf());
        if (FAILED(hr)) return nullptr;

        m_sceneMipLevels = mipLevels;
    }

    // Copy back buffer into mip level 0
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    m_context->CopySubresourceRegion(m_glassSceneTex.Get(), 0, 0, 0, 0,
                                      backBuffer.Get(), 0, nullptr);

    // Generate mipmaps — "mipmap-stacking" that replaces
    // multi-pass Gaussian/Kawase blur. SampleLevel(scene, uv, 3.0)
    // gives 1/8 resolution blur — extremely cheap to read.
    m_context->GenerateMips(m_glassSceneSRV.Get());

    return m_glassSceneSRV.Get();
}

} // namespace pfd
