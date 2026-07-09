#include "FontRenderer.h"
#include "UIRenderer.h"
#include "FrostedGlassTheme.h"
#include <d3dcompiler.h>
#include "stb_truetype.h"

namespace pfd {

// ═════════════════════════════════════════════════════════════════════════════
// Embedded shader source (matches UIRect.hlsl)
// ═════════════════════════════════════════════════════════════════════════════

static const char* g_uiVS = R"(
cbuffer CBPerFrame : register(b0) {
    float viewWidth;
    float viewHeight;
    float2 pad0;
};

struct VSIn {
    float2 quadPos      : POSITION;
    float2 instPos      : TEXCOORD0;
    float2 instSize     : TEXCOORD1;
    float4 instColor    : COLOR0;
    float  instRadius   : TEXCOORD2;
    float  instBorder   : TEXCOORD3;
    float4 instBorderClr: COLOR1;
    float  instGlow     : TEXCOORD4;
    float  instGlowInt  : TEXCOORD5;
    float  instType     : TEXCOORD6;
    float2 instUV0      : TEXCOORD7;
    float2 instUV1      : TEXCOORD8;
};

struct VSOut {
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float2 pixelSize: TEXCOORD1;
    float4 color    : COLOR0;
    float  radius   : TEXCOORD2;
    float  border   : TEXCOORD3;
    float4 borderClr: COLOR1;
    float  glow     : TEXCOORD4;
    float  glowInt  : TEXCOORD5;
    float  type     : TEXCOORD6;
    float2 uv0      : TEXCOORD7;
    float2 uv1      : TEXCOORD8;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float2 pixelPos = input.instPos + input.quadPos * input.instSize;
    
    // Fix: Add pixel snapping and 0.5 offset to align with D3D11 pixel centers
    pixelPos = floor(pixelPos) + 0.5;

    output.pos.x = (pixelPos.x / viewWidth) * 2.0 - 1.0;
    output.pos.y = 1.0 - (pixelPos.y / viewHeight) * 2.0;
    output.pos.z = 0.0;
    output.pos.w = 1.0;

    output.uv = input.quadPos;
    output.pixelSize = input.instSize;
    output.color = input.instColor;
    output.radius = input.instRadius;
    output.border = input.instBorder;
    output.borderClr = input.instBorderClr;
    output.glow = input.instGlow;
    output.glowInt = input.instGlowInt;
    output.type = input.instType;
    output.uv0 = input.instUV0;
    output.uv1 = input.instUV1;
    return output;
}
)";

static const char* g_uiPS = R"(
Texture2D    g_tex : register(t0);
SamplerState g_sam : register(s0);

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float2 pixelSize : TEXCOORD1;
    float4 color : COLOR0;
    float  radius    : TEXCOORD2;
    float  border    : TEXCOORD3;
    float4 borderClr : COLOR1;
    float  glow      : TEXCOORD4;
    float  glowInt   : TEXCOORD5;
    float  type      : TEXCOORD6;
    float2 uv0       : TEXCOORD7;
    float2 uv1       : TEXCOORD8;
};

float roundedRectSDF(float2 p, float2 halfSize, float r) {
    float2 d = abs(p) - halfSize + r;
    return length(max(d, 0.0)) - r;
}

float4 PSMain(PSIn input) : SV_TARGET {
    // Calculate text SDF unconditionally to avoid HLSL varying-flow-control errors
    float2 texUV = lerp(input.uv0, input.uv1, input.uv);
    float textDist = g_tex.Sample(g_sam, texUV).r;
    float textAa = fwidth(textDist) * 1.5;

    // ── TEXT MODE ──
    if (input.type > 0.5) {
        float edge = 0.5;
        float alpha = smoothstep(edge - textAa, edge + textAa, textDist);

        // Glow/shadow behind text
        if (input.glow > 0.0) {
            float2 off = float2(-0.003, 0.003);
            float glowDist = g_tex.Sample(g_sam, texUV + off).r;
            float glowAlpha = smoothstep(edge - textAa - 0.15, edge + textAa, glowDist);
            float4 gc = input.borderClr;
            gc.a *= glowAlpha * input.glowInt;
            float4 result = float4(input.color.rgb, alpha * input.color.a);
            result.rgb = lerp(gc.rgb, result.rgb, result.a / (result.a + gc.a + 0.001));
            result.a = max(result.a, gc.a);
            return result;
        }

        return float4(input.color.rgb, alpha * input.color.a);
    }

    // ── RECT MODE ──
    float2 p = (input.uv - 0.5) * input.pixelSize;
    float2 hs = input.pixelSize * 0.5;
    float r = min(input.radius, min(hs.x, hs.y));

    float dist = roundedRectSDF(p, hs, r);
    float aa = 1.0;

    // Fill
    float fillAlpha = 1.0 - smoothstep(-aa, aa, dist);

    // Border
    float borderAlpha = 0.0;
    if (input.border > 0.0) {
        float inner = dist + input.border;
        borderAlpha = smoothstep(-aa, aa, inner) - smoothstep(-aa, aa, dist);
        borderAlpha = clamp(borderAlpha, 0.0, 1.0);
    }

    // Glow (outside the rect)
    float glowAlpha = 0.0;
    if (input.glow > 0.0) {
        glowAlpha = smoothstep(input.glow, 0.0, dist) * input.glowInt;
    }

    // Compose layers: glow → fill → border
    float4 result = float4(0, 0, 0, 0);

    if (glowAlpha > 0.001) {
        result = float4(input.borderClr.rgb, glowAlpha * 0.5);
    }

    if (fillAlpha > 0.001) {
        float4 fl = float4(input.color.rgb, input.color.a * fillAlpha);
        result = result + fl * (1.0 - result.a);
    }

    if (borderAlpha > 0.001) {
        float4 bl = float4(input.borderClr.rgb, input.borderClr.a * borderAlpha);
        result = result + bl * (1.0 - result.a);
    }

    return result;
}
)";

// ═════════════════════════════════════════════════════════════════════════════
// Frosted Glass shader — translucent depth-based material
//
// Implements the full glass recipe:
//  1. Rounded rect SDF mask
//  2. Mipmap blur (textureLod equivalent)
//  3. Saturation boost (1.8x)
//  4. Glass tint (7% white overlay)
//  5. Specular highlight (diagonal gradient)
//  6. Border (1px, 12% white)
//
// The glass shader samples the scene texture at a mipmap level matching the
// desired blur amount. LOD 3.0 = 1/8 resolution = extremely cheap to read.
// All glass elements share ONE mipmap generation per frame.
// ═════════════════════════════════════════════════════════════════════════════

static const char* g_glassVS = R"(
cbuffer CBGlass : register(b0) {
    float viewWidth;
    float viewHeight;
    float time;
    float pad0;
};

struct VSIn {
    float2 quadPos      : POSITION;
    float2 instPos      : TEXCOORD0;
    float2 instSize     : TEXCOORD1;
    float  instRadius   : TEXCOORD2;
    float  instBlurLOD  : TEXCOORD3;
    float  instGlassAlp : TEXCOORD4;
    float  instBorderAlp: TEXCOORD5;
    float  instSpecAlp  : TEXCOORD6;
    float  instSpecLow  : TEXCOORD7;
    float  instSat      : TEXCOORD8;
};

struct VSOut {
    float4 pos       : SV_POSITION;
    float2 uv        : TEXCOORD0;
    float2 pixelSize : TEXCOORD1;
    float  radius    : TEXCOORD2;
    float  blurLOD   : TEXCOORD3;
    float  glassAlp  : TEXCOORD4;
    float  borderAlp : TEXCOORD5;
    float  specAlp   : TEXCOORD6;
    float  specLow   : TEXCOORD7;
    float  sat       : TEXCOORD8;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float2 pixelPos = input.instPos + input.quadPos * input.instSize;
    pixelPos = floor(pixelPos) + 0.5;

    output.pos.x = (pixelPos.x / viewWidth) * 2.0 - 1.0;
    output.pos.y = 1.0 - (pixelPos.y / viewHeight) * 2.0;
    output.pos.z = 0.0;
    output.pos.w = 1.0;

    output.uv = input.quadPos;
    output.pixelSize = input.instSize;
    output.radius = input.instRadius;
    output.blurLOD = input.instBlurLOD;
    output.glassAlp = input.instGlassAlp;
    output.borderAlp = input.instBorderAlp;
    output.specAlp = input.instSpecAlp;
    output.specLow = input.instSpecLow;
    output.sat = input.instSat;
    return output;
}
)";

static const char* g_glassPS = R"(
Texture2D    g_scene : register(t0);
SamplerState g_sam  : register(s0);

struct PSIn {
    float4 pos       : SV_POSITION;
    float2 uv        : TEXCOORD0;
    float2 pixelSize : TEXCOORD1;
    float  radius    : TEXCOORD2;
    float  blurLOD   : TEXCOORD3;
    float  glassAlp  : TEXCOORD4;
    float  borderAlp : TEXCOORD5;
    float  specAlp   : TEXCOORD6;
    float  specLow   : TEXCOORD7;
    float  sat       : TEXCOORD8;
};

float roundedRectSDF(float2 p, float2 halfSize, float r) {
    float2 d = abs(p) - halfSize + r;
    return length(max(d, 0.0)) - r;
}

float4 PSMain(PSIn input) : SV_TARGET {
    float2 pixel = input.uv * input.pixelSize;

    // 1. Rounded rect mask (SDF)
    float2 center = input.pixelSize * 0.5;
    float2 halfSize = input.pixelSize * 0.5;
    float2 p = pixel - center;
    float r = min(input.radius, min(halfSize.x, halfSize.y));
    float dist = roundedRectSDF(p, halfSize, r);
    if (dist > 1.0) discard;
    float edgeAlpha = 1.0 - smoothstep(-1.0, 0.0, dist);

    // 2. Mipmap blur (mipmap-stacking)
    // Instead of multi-pass Gaussian/Kawase, sample the scene texture at the
    // appropriate mipmap level. Each mip level is already a 2x-downsampled
    // (blurred) version of the previous level.
    // SampleLevel() = textureLod() in GLSL.
    float lodFloor = floor(input.blurLOD);
    float lodFrac = frac(input.blurLOD);
    float2 sceneUV = input.uv;
    float3 blurA = g_scene.SampleLevel(g_sam, sceneUV, lodFloor).rgb;
    float3 blurB = g_scene.SampleLevel(g_sam, sceneUV, lodFloor + 1.0).rgb;
    float3 blurred = lerp(blurA, blurB, lodFrac);

    // 3. Boost saturation — colors bleeding through glass become more vivid
    float luma = dot(blurred, float3(0.2126, 0.7152, 0.0722));
    blurred = lerp(float3(luma, luma, luma), blurred, input.sat);

    // 4. Glass tint — white overlay at glassAlpha opacity
    blurred = lerp(blurred, float3(1.0, 1.0, 1.0), input.glassAlp);

    // 5. Specular highlight (135-degree diagonal gradient)
    // Top-left: brighter, bottom-right: dimmer
    float gradient = input.uv.x * 0.5 + input.uv.y * 0.5;
    float spec = lerp(input.specAlp, input.specLow, gradient);
    // Fade near edges to avoid hard cutoff
    spec *= smoothstep(0.0, 0.3, 1.0 - abs(dist) / max(halfSize.x, halfSize.y));
    blurred += float3(spec, spec, spec);

    // 6. Border — 1px using SDF edge detection
    float borderMask = smoothstep(0.0, 1.0, dist + 1.0) - smoothstep(0.0, 1.0, dist);
    blurred = lerp(blurred, float3(1.0, 1.0, 1.0), borderMask * input.borderAlp);

    // Output: RGB = composited color, Alpha = rounded rect edge alpha
    return float4(blurred, edgeAlpha);
}
)";

// ═════════════════════════════════════════════════════════════════════════════
// Background blob shader — soft colored circles behind the UI layer
// ═════════════════════════════════════════════════════════════════════════════

static const char* g_blobVS = R"(
cbuffer CBBlob : register(b0) {
    float viewWidth;
    float viewHeight;
    float time;
    float pad0;
};

struct VSIn {
    float2 quadPos : POSITION;
    float2 center  : TEXCOORD0;
    float  radius  : TEXCOORD1;
    float4 color   : COLOR0;
};

struct VSOut {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float2 center : TEXCOORD1;
    float  radius : TEXCOORD2;
    float4 color  : COLOR0;
};

VSOut VSMain(VSIn input) {
    VSOut o;
    // Expand quad to cover the blob area
    float2 blobCenter = input.center;
    float blobRadius = input.radius;
    float2 worldPos = blobCenter + (input.quadPos - 0.5) * 2.0 * blobRadius;

    o.pos.x = worldPos.x * 2.0 - 1.0;
    o.pos.y = 1.0 - worldPos.y * 2.0;
    o.pos.z = 0.0;
    o.pos.w = 1.0;
    o.uv = input.quadPos;
    o.center = input.center;
    o.radius = input.radius;
    o.color = input.color;
    return o;
}
)";

static const char* g_blobPS = R"(
struct PSIn {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float2 center : TEXCOORD1;
    float  radius : TEXCOORD2;
    float4 color  : COLOR0;
};

float4 PSMain(PSIn input) : SV_TARGET {
    float d = distance(input.uv, float2(0.5, 0.5));
    float alpha = smoothstep(0.5, 0.5 * 0.3, d) * input.color.a;
    return float4(input.color.rgb, alpha);
}
)";

// ═════════════════════════════════════════════════════════════════════════════
// Initialization
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* /*ctx*/) {
    m_instances.reserve(m_maxInstances);
    m_glassInstances.reserve(256);
    if (!CreateShaders(device)) return false;
    if (!CreateGeometry(device)) return false;
    if (!CreateStates(device)) return false;
    if (!CreateGlassShaders(device)) return false;
    if (!CreateGlassGeometry(device)) return false;
    return true;
}

void UIRenderer::Shutdown() {
    m_instances.clear();
    m_glyphs.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Shader compilation
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::CreateShaders(ID3D11Device* device) {
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    HRESULT hr;

    hr = D3DCompile(g_uiVS, strlen(g_uiVS), "uiVS", nullptr, nullptr,
                            "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(g_uiPS, strlen(g_uiPS), "uiPS", nullptr, nullptr,
                    "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                     nullptr, m_vs.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                    nullptr, m_ps.GetAddressOf());
    if (FAILED(hr)) return false;

    // Input layout: per-vertex (quad) + per-instance data
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,                             D3D11_INPUT_PER_VERTEX_DATA,   0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       1, 0,                             D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,          1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,          1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"COLOR",    1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,          1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT,          1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT,          1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 7, DXGI_FORMAT_R32G32_FLOAT,       1, 72,                            D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 8, DXGI_FORMAT_R32G32_FLOAT,       1, 80,                            D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

    hr = device->CreateInputLayout(layout, _countof(layout),
                                    vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                    m_layout.GetAddressOf());
    if (FAILED(hr)) return false;
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Geometry (unit quad + instance buffer)
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::CreateGeometry(ID3D11Device* device) {
    // Unit quad: (0,0) to (1,1)
    float quadVerts[] = {
        0, 0,   1, 0,   1, 1,
        0, 0,   1, 1,   0, 1,
    };

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(quadVerts);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = quadVerts;
    HRESULT hr = device->CreateBuffer(&bd, &init, m_quadVB.GetAddressOf());
    if (FAILED(hr)) return false;

    // Dynamic instance buffer
    bd.ByteWidth = (UINT)(m_maxInstances * sizeof(InstanceData));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_instanceVB.GetAddressOf());
    if (FAILED(hr)) return false;

    // Constant buffer
    bd.ByteWidth = sizeof(CBPerFrame);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&bd, nullptr, m_cbPerFrame.GetAddressOf());
    return SUCCEEDED(hr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Render states
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::CreateStates(ID3D11Device* device) {
    // Alpha blend
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device->CreateBlendState(&bd, m_blendState.GetAddressOf());
    if (FAILED(hr)) return false;

    // Additive blend
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    hr = device->CreateBlendState(&bd, m_additiveBlendState.GetAddressOf());
    if (FAILED(hr)) return false;

    // Rasterizer (no cull, solid fill)
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    hr = device->CreateRasterizerState(&rs, m_rasterizer.GetAddressOf());
    if (FAILED(hr)) return false;

    // Depth stencil (no depth)
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE;
    hr = device->CreateDepthStencilState(&ds, m_depthStencil.GetAddressOf());
    if (FAILED(hr)) return false;

    // Sampler (bilinear, clamp)
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, m_sampler.GetAddressOf());
    return SUCCEEDED(hr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Font atlas — SDF generation via stb_truetype
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::LoadFont(ID3D11Device* device, ID3D11DeviceContext* ctx,
                           const char* ttfPath, float fontSize) {
    FILE* f = nullptr;
    fopen_s(&f, ttfPath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return false; }

    std::vector<unsigned char> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);

    return BuildFontAtlas(device, ctx, data.data(), fontSize);
}

bool UIRenderer::BuildFontAtlas(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                 const unsigned char* ttfData, float fontSize) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttfData, stbtt_GetFontOffsetForIndex(ttfData, 0)))
        return false;

    float scale = stbtt_ScaleForPixelHeight(&font, fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    m_fontLineHeight = (ascent - descent + lineGap) * scale;

    // SDF parameters
    int padding = 6;      // extra padding around glyphs for SDF
    // float onEdge = 0.25f; // SDF value at the glyph edge (used by stbtt_GetGlyphSDF internally)
    float pixelDistScale = 64.0f; // how fast SDF falls off

    m_atlasW = 4096;
    m_atlasH = 1024;
    std::vector<unsigned char> atlas(m_atlasW * m_atlasH, 0);

    int penX = padding;
    int baseline = (int)(ascent * scale) + padding;
    int maxY = 0;

    for (int ch = 32; ch < 127; ch++) {
        int glyphIdx = stbtt_FindGlyphIndex(&font, ch);
        int ax, lsb;
        stbtt_GetGlyphHMetrics(&font, glyphIdx, &ax, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetGlyphBox(&font, glyphIdx, &x0, &y0, &x1, &y1);
        int gw = (int)((x1 - x0) * scale) + padding * 2;
        int gh = (int)((y1 - y0) * scale) + padding * 2;

        if (penX + gw + padding >= m_atlasW) {
            penX = padding;
        }

        GlyphInfo gi{};
        if (gw > 0 && gh > 0) {
            // Generate SDF for this glyph
            int sdfW = gw, sdfH = gh;
            unsigned char* sdf = stbtt_GetGlyphSDF(
                &font, scale, glyphIdx, padding,
                128, pixelDistScale,
                &sdfW, &sdfH, nullptr, nullptr);

            if (sdf) {
                // Revert previous SDF byte flipping, as flipping UV coordinates is the correct approach
                for (int gy = 0; gy < sdfH; gy++) {
                    for (int gx = 0; gx < sdfW; gx++) {
                        int ax2 = penX + gx;
                        int ay = baseline + (int)(y0 * scale) + gy;
                        if (ax2 >= 0 && ax2 < m_atlasW && ay >= 0 && ay < m_atlasH) {
                            atlas[ay * m_atlasW + ax2] = sdf[gy * sdfW + gx];
                        }
                    }
                }
                stbtt_FreeSDF(sdf, nullptr);
            }

            gi.u0 = (float)penX / m_atlasW;
            gi.v0 = (float)(baseline + (int)(y0 * scale)) / m_atlasH;
            gi.u1 = (float)(penX + sdfW) / m_atlasW;
            gi.v1 = (float)(baseline + (int)(y0 * scale) + sdfH) / m_atlasH;
            gi.pw = sdfW;
            gi.ph = sdfH;
            gi.xoff = (int)(x0 * scale) - padding;
            gi.yoff = (int)(y0 * scale) - padding; // Fix: Reverted incorrect yoff calculation
            gi.advance = (int)(ax * scale);

            if (baseline + (int)(y0 * scale) + sdfH > maxY)
                maxY = baseline + (int)(y0 * scale) + sdfH;

            penX += sdfW + padding;
        } else {
            gi.u0 = gi.v0 = gi.u1 = gi.v1 = 0;
            gi.pw = gi.ph = 0;
            gi.xoff = gi.yoff = 0;
            gi.advance = (int)(ax * scale);
        }

        m_glyphs[ch] = gi;
    }

    m_fontScale = 20.0f / fontSize; // match old FontRenderer base scale

    // Create D3D texture
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = m_atlasW;
    desc.Height = m_atlasH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = atlas.data();
    init.SysMemPitch = m_atlasW;

    HRESULT hr = device->CreateTexture2D(&desc, &init, m_fontAtlasTex.GetAddressOf());
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device->CreateShaderResourceView(m_fontAtlasTex.Get(), &srvDesc, m_fontAtlasSRV.GetAddressOf());
    return SUCCEEDED(hr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Frame lifecycle
// ═════════════════════════════════════════════════════════════════════════════

void UIRenderer::Begin(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH) {
    m_ctx = ctx;
    m_viewW = viewW;
    m_viewH = viewH;
    m_instances.clear();
    m_glassInstances.clear();
    m_sceneSRV = nullptr;
    m_maxSceneLOD = 0.0f;
}

void UIRenderer::Begin(ID3D11DeviceContext* ctx, uint32_t viewW, uint32_t viewH,
                       ID3D11ShaderResourceView* sceneSRV, float maxLOD) {
    m_ctx = ctx;
    m_viewW = viewW;
    m_viewH = viewH;
    m_instances.clear();
    m_glassInstances.clear();
    m_sceneSRV = sceneSRV;
    m_maxSceneLOD = maxLOD;
}

void UIRenderer::End() {
    // Glass first (behind regular UI), then regular rects/text
    FlushGlassBatch();
    FlushBatch();
    m_ctx = nullptr;
}

// ═════════════════════════════════════════════════════════════════════════════
// Drawing
// ═════════════════════════════════════════════════════════════════════════════

void UIRenderer::DrawRect(float x, float y, float w, float h, const RectStyle& style) {
    if (m_instances.size() >= m_maxInstances) FlushBatch();

    InstanceData inst{};
    inst.position = {x, y};
    inst.size = {w, h};
    inst.color = style.color;
    inst.cornerRadius = style.cornerRadius;
    inst.borderWidth = style.borderWidth;
    inst.borderColor = style.borderColor;
    inst.glowSize = style.glowSize;
    inst.glowIntensity = style.glowIntensity;
    inst.type = 0.0f;
    inst.uv0 = {0, 0};
    inst.uv1 = {0, 0};
    m_instances.push_back(inst);
}

void UIRenderer::DrawRect(float x, float y, float w, float h,
                           util::Vec4 color, float cornerRadius) {
    RectStyle style;
    style.color = color;
    style.cornerRadius = cornerRadius;
    DrawRect(x, y, w, h, style);
}

void UIRenderer::DrawText(const std::string& text, float x, float y,
                           util::Vec4 color, float scale) {
    DrawTextWithGlow(text, x, y, color, {0, 0, 0, 0}, 0, 0, scale);
}

void UIRenderer::DrawTextWithGlow(const std::string& text, float x, float y,
                                   util::Vec4 color, util::Vec4 glowColor,
                                   float glowSize, float glowIntensity,
                                   float scale) {
    if (!m_fontAtlasSRV || text.empty()) return;
    if (m_instances.size() >= m_maxInstances) FlushBatch();

    float s = scale * m_fontScale;
    float cx = x;

    for (char c : text) {
        int ch = (unsigned char)c;
        if (ch < 32 || ch > 126) {
            auto sp = m_glyphs.find(' ');
            if (sp != m_glyphs.end()) cx += sp->second.advance * s;
            else cx += 8 * s;
            continue;
        }

        auto it = m_glyphs.find(ch);
        if (it == m_glyphs.end()) { cx += 8 * s; continue; }

        auto& gi = it->second;
        if (gi.pw > 0 && gi.ph > 0) {
            if (m_instances.size() >= m_maxInstances) FlushBatch();

            float gx = cx + gi.xoff * s;
            float gy = y + gi.yoff * s;
            float gw = gi.pw * s;
            float gh = gi.ph * s;

            InstanceData inst{};
            inst.position = {gx, gy};
            inst.size = {gw, gh};
            inst.color = color;
            inst.cornerRadius = 0;
            inst.borderWidth = 0;
            inst.borderColor = glowColor;
            inst.glowSize = glowSize;
            inst.glowIntensity = glowIntensity;
            inst.type = 1.0f; // text
            inst.uv0 = {gi.u0, gi.v0};
            inst.uv1 = {gi.u1, gi.v1};
            m_instances.push_back(inst);
        }
        cx += gi.advance * s;
    }
}

float UIRenderer::GetTextWidth(const std::string& text, float scale) {
    float s = scale * m_fontScale;
    float w = 0;
    for (char c : text) {
        int ch = (unsigned char)c;
        auto it = m_glyphs.find(ch);
        if (it != m_glyphs.end()) w += it->second.advance * s;
        else {
            auto sp = m_glyphs.find(' ');
            if (sp != m_glyphs.end()) w += sp->second.advance * s;
            else w += 8 * s;
        }
    }
    return w;
}

float UIRenderer::GetLineHeight(float scale) {
    return m_fontLineHeight * m_fontScale * scale;
}

// ═════════════════════════════════════════════════════════════════════════════
// DrawGlass — frosted translucent panel
// ═════════════════════════════════════════════════════════════════════════════

void UIRenderer::DrawGlass(float x, float y, float w, float h,
                           const RectStyle& style, float blurLOD, float glassAlpha) {
    if (m_glassInstances.size() >= 2048) FlushGlassBatch();

    // Clamp blurLOD to available mip levels
    float effectiveLOD = (blurLOD > m_maxSceneLOD) ? m_maxSceneLOD : blurLOD;

    GlassInstanceData inst{};
    inst.position = {x, y};
    inst.size = {w, h};
    inst.cornerRadius = style.cornerRadius;
    inst.blurLOD = effectiveLOD;
    inst.glassAlpha = glassAlpha;
    inst.borderAlpha = style.borderColor.w;
    inst.specularAlpha = glass::GetTheme().specularAlpha;
    inst.specularAlphaLow = glass::GetTheme().specularAlphaLow;
    inst.saturation = glass::GetTheme().saturation;
    m_glassInstances.push_back(inst);
}

void UIRenderer::FlushGlass() {
    FlushGlassBatch();
}

void UIRenderer::FlushGlassBatch() {
    if (m_glassInstances.empty() || !m_ctx || !m_sceneSRV || !m_glassVS) return;

    // Upload instances
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_ctx->Map(m_glassInstanceVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, m_glassInstances.data(), m_glassInstances.size() * sizeof(GlassInstanceData));
    m_ctx->Unmap(m_glassInstanceVB.Get(), 0);

    // Update constant buffer
    CBPerFrame cb{(float)m_viewW, (float)m_viewH, m_time, 0};
    hr = m_ctx->Map(m_glassCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, &cb, sizeof(cb));
    m_ctx->Unmap(m_glassCB.Get(), 0);

    // Bind pipeline
    m_ctx->IASetInputLayout(m_glassLayout.Get());
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT strides[2] = {sizeof(float) * 2, sizeof(GlassInstanceData)};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer* bufs[2] = {m_quadVB.Get(), m_glassInstanceVB.Get()};
    m_ctx->IASetVertexBuffers(0, 2, bufs, strides, offsets);

    m_ctx->VSSetShader(m_glassVS.Get(), nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, m_glassCB.GetAddressOf());

    m_ctx->PSSetShader(m_glassPS.Get(), nullptr, 0);
    m_ctx->PSSetShaderResources(0, 1, &m_sceneSRV);
    m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

    float bf[4] = {1,1,1,1};
    m_ctx->OMSetBlendState(m_blendState.Get(), bf, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    m_ctx->RSSetState(m_rasterizer.Get());

    m_ctx->DrawInstanced(6, (UINT)m_glassInstances.size(), 0, 0);

    m_glassInstances.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Glass shader compilation
// ═════════════════════════════════════════════════════════════════════════════

bool UIRenderer::CreateGlassShaders(ID3D11Device* device) {
    ComPtr<ID3DBlob> vsBlob, psBlob, err;
    HRESULT hr;

    hr = D3DCompile(g_glassVS, strlen(g_glassVS), "glassVS", nullptr, nullptr,
                    "VSMain", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = D3DCompile(g_glassPS, strlen(g_glassPS), "glassPS", nullptr, nullptr,
                    "PSMain", "ps_5_0", 0, 0, psBlob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                     nullptr, m_glassVS.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                    nullptr, m_glassPS.GetAddressOf());
    if (FAILED(hr)) return false;

    // Input layout for glass instances
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 6, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 7, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"TEXCOORD", 8, DXGI_FORMAT_R32_FLOAT,    1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };

    hr = device->CreateInputLayout(layout, _countof(layout),
                                    vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                    m_glassLayout.GetAddressOf());
    return SUCCEEDED(hr);
}

bool UIRenderer::CreateGlassGeometry(ID3D11Device* device) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(2048 * sizeof(GlassInstanceData));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&bd, nullptr, m_glassInstanceVB.GetAddressOf());
    if (FAILED(hr)) return false;

    bd.ByteWidth = sizeof(CBPerFrame);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&bd, nullptr, m_glassCB.GetAddressOf());
    return SUCCEEDED(hr);
}

// ═════════════════════════════════════════════════════════════════════════════
// Flush: upload instances + draw
// ═════════════════════════════════════════════════════════════════════════════

void UIRenderer::FlushBatch() {
    if (m_instances.empty() || !m_ctx) return;
    if (!m_instanceVB) {
        m_instances.clear(); // Prevent infinite growth
        return; 
    }

    // Upload instances
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_ctx->Map(m_instanceVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, m_instances.data(), m_instances.size() * sizeof(InstanceData));
    m_ctx->Unmap(m_instanceVB.Get(), 0);

    // Update constant buffer
    CBPerFrame cb{(float)m_viewW, (float)m_viewH, 0, 0};
    hr = m_ctx->Map(m_cbPerFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;
    memcpy(mapped.pData, &cb, sizeof(cb));
    m_ctx->Unmap(m_cbPerFrame.Get(), 0);

    // Bind pipeline
    m_ctx->IASetInputLayout(m_layout.Get());
    m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT strides[2] = {sizeof(float) * 2, sizeof(InstanceData)};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer* bufs[2] = {m_quadVB.Get(), m_instanceVB.Get()};
    m_ctx->IASetVertexBuffers(0, 2, bufs, strides, offsets);

    m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    m_ctx->VSSetConstantBuffers(0, 1, m_cbPerFrame.GetAddressOf());

    m_ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    // Bind font atlas as t0 (for text), or a white texture if no font loaded
    ID3D11ShaderResourceView* srv = m_fontAtlasSRV.Get();
    m_ctx->PSSetShaderResources(0, 1, &srv);
    m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

    float bf[4] = {1,1,1,1};
    m_ctx->OMSetBlendState(m_blendState.Get(), bf, 0xFFFFFFFF);
    m_ctx->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    m_ctx->RSSetState(m_rasterizer.Get());

    m_ctx->DrawInstanced(6, (UINT)m_instances.size(), 0, 0);

    m_instances.clear();
}

} // namespace pfd
