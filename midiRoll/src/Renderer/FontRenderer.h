#pragma once
#include "D3DContext.h"
#include "SpriteBatch.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace pfd {

class FontRenderer {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx, float fontSize = 24.0f);
    void DrawText(SpriteBatch& batch, const std::string& text, float x, float y,
                  float r = 1.0f, float g = 1.0f, float b = 1.0f, float scale = 1.0f);
    float GetTextWidth(const std::string& text, float scale = 1.0f);
    float GetLineHeight() const { return m_lineHeight * m_scale; }
    void SetScale(float s) { m_scale = s; }
    ID3D11ShaderResourceView* GetTexture() const { return m_atlasSRV; }

private:
    struct GlyphInfo {
        float x0, y0, x1, y1; // UV coords in atlas
        int   pw, ph;          // pixel width/height
        int   xoff, yoff;      // offset from baseline
        int   advance;         // horizontal advance
    };

    ID3D11Device*        m_device{};
    ID3D11DeviceContext*  m_ctx{};
    ID3D11Texture2D*     m_atlasTex{};
    ID3D11ShaderResourceView* m_atlasSRV{};

    std::unordered_map<int, GlyphInfo> m_glyphs;
    int   m_atlasW{}, m_atlasH{};
    float m_lineHeight{};
    float m_scale{1.0f};
    float m_fontSize{};

    bool LoadFont();
    bool BuildAtlas(const unsigned char* ttfData);
    static const unsigned char* GetDefaultFontData();
};

} // namespace pfd
