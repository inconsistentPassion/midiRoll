#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace pfd {

class TextRenderer {
public:
    bool Initialize(HWND hwnd);
    void BeginDraw();
    void EndDraw();
    void DrawText(const std::wstring& text, float x, float y, float r, float g, float b, float a = 1.0f);
    void DrawText(const std::wstring& text, float x, float y, D2D1_COLOR_F color);
    void Resize(uint32_t width, uint32_t height);

private:
    ComPtr<ID2D1Factory>           m_d2dFactory;
    ComPtr<IDWriteFactory>         m_dwriteFactory;
    ComPtr<ID2D1HwndRenderTarget>  m_d2dRT;
    ComPtr<IDWriteTextFormat>      m_textFormat;
    ComPtr<ID2D1SolidColorBrush>   m_brush;

    void CreateResources();
};

} // namespace pfd
