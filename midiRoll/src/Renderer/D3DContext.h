#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace pfd {

class D3DContext {
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);

    void Resize(uint32_t width, uint32_t height);
    void Present(bool vsync);
    void Clear(float r, float g, float b, float a = 1.0f);

    ID3D11Device*           Device()        const { return m_device.Get(); }
    ID3D11DeviceContext*    Context()       const { return m_context.Get(); }
    IDXGISwapChain*         SwapChain()     const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* BackBufferRTV() const { return m_backBufferRTV.Get(); }
    ID3D11DepthStencilView* DepthStencil() const { return m_depthStencil.Get(); }
    uint32_t                Width()         const { return m_width; }
    uint32_t                Height()        const { return m_height; }

    ID3D11ShaderResourceView* CaptureScreen();

private:
    void CreateDepthStencil(uint32_t w, uint32_t h);
    
    ComPtr<ID3D11Texture2D>          m_sceneCopyTex;
    ComPtr<ID3D11ShaderResourceView> m_sceneCopySRV;

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>     m_context;
    ComPtr<IDXGISwapChain>         m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;
    ComPtr<ID3D11Texture2D>        m_depthStencilTex;
    ComPtr<ID3D11DepthStencilView> m_depthStencil;
    D3D_FEATURE_LEVEL              m_featureLevel{};
    uint32_t                       m_width{};
    uint32_t                       m_height{};
};

} // namespace pfd
