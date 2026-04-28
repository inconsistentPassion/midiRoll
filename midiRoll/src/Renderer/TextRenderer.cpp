#include "TextRenderer.h"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace pfd {

bool TextRenderer::Initialize(HWND hwnd) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        24.0f, L"en-us", m_textFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Get client rect for render target size
    RECT rc;
    GetClientRect(hwnd, &rc);

    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)
    );

    hr = m_d2dFactory->CreateHwndRenderTarget(&rtProps, &hwndProps, m_d2dRT.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = m_d2dRT->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1), m_brush.GetAddressOf());
    if (FAILED(hr)) return false;

    return true;
}

void TextRenderer::Resize(uint32_t width, uint32_t height) {
    if (m_d2dRT) {
        m_d2dRT->Resize(D2D1::SizeU(width, height));
    }
}

void TextRenderer::BeginDraw() {
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
