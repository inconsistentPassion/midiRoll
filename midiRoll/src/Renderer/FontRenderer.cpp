#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "FontRenderer.h"
#include <algorithm>
#include <cstring>

namespace pfd {

bool FontRenderer::LoadFont() {
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\verdana.ttf",
        "C:\\Windows\\Fonts\\calibri.ttf",
        nullptr
    };

    for (int i = 0; fontPaths[i]; i++) {
        FILE* f = nullptr;
        fopen_s(&f, fontPaths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 0) {
            std::vector<unsigned char> data(size);
            fread(data.data(), 1, size, f);
            fclose(f);
            return BuildAtlas(data.data());
        }
        fclose(f);
    }
    return false;
}

bool FontRenderer::BuildAtlas(const unsigned char* ttfData) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttfData, stbtt_GetFontOffsetForIndex(ttfData, 0)))
        return false;

    float scale = stbtt_ScaleForPixelHeight(&font, m_fontSize);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    m_lineHeight = (ascent - descent + lineGap) * scale;

    // Larger atlas for sharper text
    m_atlasW = 4096;
    m_atlasH = 1024;
    std::vector<unsigned char> bitmap(m_atlasW * m_atlasH, 0);

    int penX = 2;
    int baseline = (int)(ascent * scale) + 3;

    for (int ch = 32; ch < 127; ch++) {
        int glyphIdx = stbtt_FindGlyphIndex(&font, ch);
        int ax, lsb;
        stbtt_GetGlyphHMetrics(&font, glyphIdx, &ax, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&font, glyphIdx, scale, scale, &x0, &y0, &x1, &y1);
        int gw = x1 - x0;
        int gh = y1 - y0;

        if (penX + gw + 2 >= m_atlasW) { penX = 2; }

        if (gw > 0 && gh > 0) {
            std::vector<unsigned char> glyph(gw * gh);
            stbtt_MakeGlyphBitmap(&font, glyph.data(), gw, gh, gw, scale, scale, glyphIdx);
            for (int gy = 0; gy < gh; gy++) {
                for (int gx = 0; gx < gw; gx++) {
                    int ax2 = penX + gx;
                    int ay = baseline + y0 + gy;
                    if (ax2 >= 0 && ax2 < m_atlasW && ay >= 0 && ay < m_atlasH)
                        bitmap[ay * m_atlasW + ax2] = glyph[gy * gw + gx];
                }
            }
        }

        GlyphInfo gi;
        gi.x0 = (float)penX / m_atlasW;
        gi.y0 = (float)(baseline + y0) / m_atlasH;
        gi.x1 = (float)(penX + gw) / m_atlasW;
        gi.y1 = (float)(baseline + y0 + gh) / m_atlasH;
        gi.pw = gw;
        gi.ph = gh;
        gi.xoff = x0;
        gi.yoff = y0;
        gi.advance = (int)(ax * scale);
        m_glyphs[ch] = gi;

        penX += gw + 3;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = m_atlasW;
    desc.Height = m_atlasH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = bitmap.data();
    initData.SysMemPitch = m_atlasW;

    HRESULT hr = m_device->CreateTexture2D(&desc, &initData, &m_atlasTex);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = m_device->CreateShaderResourceView(m_atlasTex, &srvDesc, &m_atlasSRV);
    return SUCCEEDED(hr);
}

bool FontRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, float fontSize) {
    m_device = device;
    m_ctx = ctx;
    m_fontSize = fontSize > 0 ? fontSize : 48.0f;
    m_baseScale = 20.0f / m_fontSize; // keep old 20px scale factors working
    return LoadFont();
}

void FontRenderer::DrawText(SpriteBatch& batch, const std::string& text, float x, float y,
                             float r, float g, float b, float scale) {
    if (!m_atlasSRV) return;
    float s = scale * m_scale * m_baseScale;
    float cx = x;

    for (char c : text) {
        int ch = (unsigned char)c;
        if (ch < 32 || ch > 126) { cx += 8 * s; continue; }

        auto it = m_glyphs.find(ch);
        if (it == m_glyphs.end()) { cx += 8 * s; continue; }

        auto& gi = it->second;
        if (gi.pw > 0 && gi.ph > 0) {
            float gx = cx + gi.xoff * s;
            float gy = y + gi.yoff * s;
            float gw = gi.pw * s;
            float gh = gi.ph * s;

            batch.Draw({gx, gy}, {gw, gh}, m_atlasSRV,
                       {gi.x0, gi.y0}, {gi.x1, gi.y1}, {r, g, b, 1.0f});
        }
        cx += gi.advance * s;
    }
}

float FontRenderer::GetTextWidth(const std::string& text, float scale) {
    float w = 0;
    float s = scale * m_scale * m_baseScale;
    for (char c : text) {
        int ch = (unsigned char)c;
        auto it = m_glyphs.find(ch);
        if (it != m_glyphs.end()) w += it->second.advance * s;
        else w += 8 * s;
    }
    return w;
}

} // namespace pfd
