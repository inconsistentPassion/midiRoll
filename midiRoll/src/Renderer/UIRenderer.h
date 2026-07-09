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
    // Begin with glass scene texture (mipmap scene capture for glass blur)
    void Begin(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH,
               ID3D11ShaderResourceView* sceneSRV, float maxLOD);
    void End();

    // ── Rect drawing ──
    struct RectStyle {
        util::Vec4 color{0.1f, 0.1f, 0.15f, 0.9f};
        float      cornerRadius = 12.0f;
        float      borderWidth  = 0.0f;
        util::Vec4 borderColor{1, 1, 1, 0.3f};
        float      glowSize     = 0.0f;
        float      glowIntensity = 0.0f;
        // ── Frosted Glass (translucent depth-based material) ──
        // When true, the surface renders as frosted translucent glass: a soft
      // top specular sheen, subtle animated frost noise, and a directional
        // (top-brighter) rim highlight are layered on top of `color`/`borderColor`.
        bool       glass = false;
        float      specularStrength = 1.0f; // 0 = flat glass, 1 = full sheen
    };

    void DrawRect(float x, float y, float w, float h, const RectStyle& style);
    void DrawRect(float x, float y, float w, float h, util::Vec4 color,
                  float cornerRadius = 12.0f);

    // ── Frosted Glass convenience helper ──
    // Draws a frosted translucent panel/button/card.
    // Uses mipmap-based blur (scene texture sampled at appropriate LOD level).
    void DrawGlass(float x, float y, float w, float h, const RectStyle& style,
                   float blurLOD = 3.0f, float glassAlpha = 0.07f);

    // ── Glass batch flush (must be called before non-glass drawing) ──
    void FlushGlass();

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
        float      type;     // 0=rect, 1=text, 2=frosted glass rect
        float      specular; // frosted glass specular sheen strength
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

    // Glass-specific data (separate from regular instances)
    struct GlassInstanceData {
        util::Vec2 position;
        util::Vec2 size;
        float      cornerRadius;
        float      blurLOD;        // mipmap level for blur (3.0 standard, 1.5 light)
        float      glassAlpha;     // tint opacity (0.07 dark)
        float      borderAlpha;    // border opacity (0.12 base)
        float      specularAlpha;  // specular highlight (0.07 top-left)
        float      specularAlphaLow; // (0.025 bottom-right)
        float      saturation;     // saturation boost (1.8)
        float      pad[2];         // alignment
    };

    // D3D11 resources — regular rect/text pipeline
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

    // D3D11 resources — glass pipeline (separate shaders)
    ComPtr<ID3D11VertexShader>       m_glassVS;
    ComPtr<ID3D11PixelShader>        m_glassPS;
    ComPtr<ID3D11InputLayout>        m_glassLayout;
    ComPtr<ID3D11Buffer>             m_glassInstanceVB;
    ComPtr<ID3D11Buffer>             m_glassCB;
    ID3D11ShaderResourceView*        m_sceneSRV = nullptr; // weak ref, owned by D3DContext
    float                            m_maxSceneLOD = 0.0f;

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
    std::vector<GlassInstanceData> m_glassInstances;

    // Helpers
    bool CreateShaders(ID3D11Device* device);
    bool CreateGeometry(ID3D11Device* device);
    bool CreateStates(ID3D11Device* device);
    bool CreateGlassShaders(ID3D11Device* device);
    bool CreateGlassGeometry(ID3D11Device* device);
    bool BuildFontAtlas(ID3D11Device* device, ID3D11DeviceContext* ctx,
                        const unsigned char* ttfData, float fontSize);
    void FlushBatch();
    void FlushGlassBatch();
};

} // namespace pfd
