#pragma once
#include "D3DContext.h"

namespace pfd {

class RenderTarget {
public:
    bool Create(ID3D11Device* device, uint32_t width, uint32_t height,
                DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
    void Clear(ID3D11DeviceContext* ctx, float r, float g, float b, float a = 1.0f);
    void Bind(ID3D11DeviceContext* ctx);
    void Unbind(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* restoreRTV);

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }
    uint32_t Width()  const { return m_width; }
    uint32_t Height() const { return m_height; }

private:
    ComPtr<ID3D11Texture2D>          m_texture;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    uint32_t m_width{}, m_height{};
};

} // namespace pfd
