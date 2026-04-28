#include "TextRenderer.h"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace pfd {

bool TextRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, IDXGISwapChain* swapChain) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));

    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        24.0f, L"en-us", m_textFormat.GetAddressOf()
    );
    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    return CreateD2DResources(device, swapChain);
}

bool TextRenderer::CreateD2DResources(ID3D11Device* device, IDXGISwapChain* swapChain) {
    swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.GetAddressOf()));

    ComPtr<IDXGISurface> surface;
    m_backBuffer.As(&surface);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    HRESULT hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(surface.Get(), &props, m_d2dRT.GetAddressOf());
    if (FAILED(hr)) return false;

    m_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1), m_brush.GetAddressOf());
    return true;
}

void TextRenderer::BeginDraw(ID3D11DeviceContext*) {
    m_d2dRT->BeginDraw();
}

void TextRenderer::EndDraw() {
    m_d2dRT->EndDraw();
}

void TextRenderer::DrawText(const std::wstring& text, float x, float y,
                              float r, float g, float b, float a) {
    m_brush->SetColor(D2D1::ColorF(r, g, b, a));
    D2D1_RECT_F rc = D2D1::RectF(x, y, x + 2000, y + 100);
    m_d2dRT->DrawText(text.c_str(), (UINT32)text.size(), m_textFormat.Get(), rc, m_brush.Get());
}

void TextRenderer::DrawText(const std::wstring& text, float x, float y, D2D1_COLOR_F color) {
    DrawText(text, x, y, color.r, color.g, color.b, color.a);
}

} // namespace pfd
