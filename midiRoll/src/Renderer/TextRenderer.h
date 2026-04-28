#pragma once
#include "D3DContext.h"
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace pfd {

class TextRenderer {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain);
    void BeginDraw(ID3D11DeviceContext* ctx);
    void EndDraw();
    void DrawText(const std::wstring& text, float x, float y, float r, float g, float b, float a = 1.0f);
    void DrawText(const std::wstring& text, float x, float y, D2D1_COLOR_F color);

private:
    ComPtr<ID2D1Factory>           m_d2dFactory;
    ComPtr<IDWriteFactory>         m_dwriteFactory;
    ComPtr<ID2D1RenderTarget>      m_d2dRT;
    ComPtr<IDWriteTextFormat>      m_textFormat;
    ComPtr<ID2D1SolidColorBrush>   m_brush;
    ComPtr<ID3D11Texture2D>        m_backBuffer;

    bool CreateD2DResources(ID3D11Device* device, IDXGISwapChain* swapChain);
};

} // namespace pfd
