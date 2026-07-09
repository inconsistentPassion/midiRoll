#pragma once
#include "D3DContext.h"
#include "../Util/Math.h"
#include "../Util/Color.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace pfd {

// ─── UIRenderer: instanced rounded-rect + SDF text renderer ─────────────────
// Completely independent from SpriteBatch. Own shaders, own pipeline.
//
// Usage:
//   ui.Begin(ctx, viewW, viewH);
//   ui.DrawRect(x, y, w, h, color, cornerRadius, borderWidth, borderColor);
//   ui.DrawText(text, x, y, color, scale);
//   ui.End();
// ─────────────────────────────────────────────────────────────────────────────

#ifdef DrawText
#undef DrawText
#endif

class UIRenderer {
public:
    // ── Lifecycle ──
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    // ── Frame ──
    void Begin(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH);
    void End();

    // ── Rect drawing ──
    struct RectStyle {
        util::Vec4 color{0.1f, 0.1f, 0.15f, 0.9f};
        float      cornerRadius = 12.0f;
        float      borderWidth  = 0.0f;
        util::Vec4 borderColor{1, 1, 1, 0.3f};
        float      glowSize     = 0.0f;
        float      glowIntensity = 0.0f;
        // ── Liquid Glass (macOS 26 "Tahoe" style) ──
        // When true, the surface renders as frosted translucent glass: a soft
      // top specular sheen, subtle animated frost noise, and a directional
        // (top-brighter) rim highlight are layered on top of `color`/`borderColor`.
        bool       glass = false;
        float      specularStrength = 1.0f; // 0 = flat glass, 1 = full sheen
    };

    void DrawRect(float x, float y, float w, float h, const RectStyle& style);
    void DrawRect(float x, float y, float w, float h, util::Vec4 color,
                  float cornerRadius = 12.0f);

    // ── Liquid Glass convenience helper ──
    // Draws a frosted "Liquid Glass" panel/button/card in the macOS 26 Tahoe style.
    void DrawGlass(float x, float y, float w, float h, const RectStyle& style);

    // ── Text drawing ──
    void DrawText(const std::string& text, float x, float y,
                  util::Vec4 color, float scale = 1.0f);
    void DrawTextWithGlow(const std::string& text, float x, float y,
                          util::Vec4 color, util::Vec4 glowColor,
                          float glowSize = 4.0f, float glowIntensity = 0.6f,
                          float scale = 1.0f);
    float GetTextWidth(const std::string& text, float scale = 1.0f);
    float GetLineHeight(float scale = 1.0f);

    // ── Font loading ──
    bool LoadFont(ID3D11Device* device, ID3D11DeviceContext* ctx,
                  const char* ttfPath, float fontSize = 48.0f);

    // ── Direct shader access (for advanced users) ──
    ID3D11ShaderResourceView* GetFontAtlas() const { return m_fontAtlasSRV.Get(); }

private:
    struct InstanceData {
        util::Vec2 position;
        util::Vec2 size;
        util::Vec4 color;
        float      cornerRadius;
        float      borderWidth;
        util::Vec4 borderColor;
        float      glowSize;
        float      glowIntensity;
        float      type;     // 0=rect, 1=text, 2=liquid glass rect
        float      specular; // liquid glass specular sheen strength
        util::Vec2 uv0;
        util::Vec2 uv1;
    };

    struct GlyphInfo {
        float u0, v0, u1, v1; // atlas UVs
        int   pw, ph;          // pixel size
        int   xoff, yoff;      // offset from baseline
        int   advance;         // horizontal advance in pixels
    };

    struct CBPerFrame {
        float viewWidth;
        float viewHeight;
        float time; // running clock, drives the frosted-glass shimmer
        float pad;
    };

    // D3D11 resources
    ComPtr<ID3D11VertexShader>       m_vs;
    ComPtr<ID3D11PixelShader>        m_ps;
    ComPtr<ID3D11InputLayout>        m_layout;
    ComPtr<ID3D11Buffer>             m_quadVB;
    ComPtr<ID3D11Buffer>             m_instanceVB;
    ComPtr<ID3D11Buffer>             m_cbPerFrame;
    ComPtr<ID3D11BlendState>         m_blendState;
    ComPtr<ID3D11BlendState>         m_additiveBlendState;
    ComPtr<ID3D11RasterizerState>    m_rasterizer;
    ComPtr<ID3D11DepthStencilState>  m_depthStencil;
    ComPtr<ID3D11SamplerState>       m_sampler;

    // Font atlas
    ComPtr<ID3D11Texture2D>          m_fontAtlasTex;
    ComPtr<ID3D11ShaderResourceView> m_fontAtlasSRV;
    std::unordered_map<int, GlyphInfo> m_glyphs;
    int   m_atlasW = 0, m_atlasH = 0;
    float m_fontLineHeight = 0;
    float m_fontScale = 1.0f;

    // Frame state
    ID3D11DeviceContext* m_ctx = nullptr;
    uint32_t m_viewW = 0, m_viewH = 0;
    float m_time = 0.0f;
    std::vector<InstanceData> m_instances;
    size_t m_maxInstances = 16384;

    // Helpers
    bool CreateShaders(ID3D11Device* device);
    bool CreateGeometry(ID3D11Device* device);
    bool CreateStates(ID3D11Device* device);
    bool BuildFontAtlas(ID3D11Device* device, ID3D11DeviceContext* ctx,
                        const unsigned char* ttfData, float fontSize);
    void FlushBatch();
};

} // namespace pfd
