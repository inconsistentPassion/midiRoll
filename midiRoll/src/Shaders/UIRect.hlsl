// UIRect.hlsl — Unified UI shader for rounded rects, borders, glow, gradients, and SDF text
// No SpriteBatch dependency. Instanced rendering.

// ─── Vertex Shader ───────────────────────────────────────────────────────────

cbuffer CBPerFrame : register(b0) {
    float viewWidth;
    float viewHeight;
    float2 pad0;
};

struct VSIn {
    float2 quadPos  : POSITION;       // quad vertex (0,0)-(1,1)
    float2 instPos      : INST_POS;
    float2 instSize     : INST_SIZE;
    float4 instColor    : INST_COLOR;
    float  instCornerRadius : INST_RADIUS;
    float  instBorderWidth  : INST_BORDER;
    float4 instBorderColor  : INST_BORDERCLR;
    float  instGlowSize    : INST_GLOW;
    float  instGlowIntensity : INST_GLOWINT;
    float  instType        : INST_TYPE;    // 0=rect, 1=text
    float  instPad         : INST_PAD;
    float2 instUV0         : INST_UV0;     // for text: atlas UV origin
    float2 instUV1         : INST_UV1;     // for text: atlas UV end
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;   // 0..1 within the rect
    float2 pixelSize : TEXCOORD1; // rect size in pixels
    float4 color : COLOR0;
    float  cornerRadius : TEXCOORD2;
    float  borderWidth  : TEXCOORD3;
    float4 borderColor  : COLOR1;
    float  glowSize     : TEXCOORD4;
    float  glowIntensity : TEXCOORD5;
    float  type         : TEXCOORD6;
    float2 uv0          : TEXCOORD7;
    float2 uv1          : TEXCOORD8;
};

VSOut VSMain(VSIn input) {
    float2 pixelPos = input.quadPos * input.instSize + input.instPos;

    float2 ndc;
    ndc.x = (pixelPos.x / viewWidth) * 2.0 - 1.0;
    ndc.y = 1.0 - (pixelPos.y / viewHeight) * 2.0;

    VSOut o;
    o.pos   = float4(ndc, 0, 1);
    o.uv    = input.quadPos;
    o.pixelSize = input.instSize;
    o.color = input.instColor;
    o.cornerRadius = input.instCornerRadius;
    o.borderWidth  = input.instBorderWidth;
    o.borderColor  = input.instBorderColor;
    o.glowSize     = input.instGlowSize;
    o.glowIntensity = input.instGlowIntensity;
    o.type         = input.instType;
    o.uv0          = input.instUV0;
    o.uv1          = input.instUV1;
    return o;
}

// ─── Pixel Shader ────────────────────────────────────────────────────────────

Texture2D    g_tex : register(t0);
SamplerState g_sam : register(s0);

// SDF for axis-aligned rounded rectangle
float roundedRectSDF(float2 p, float2 halfSize, float radius) {
    float2 d = abs(p) - halfSize + radius;
    return length(max(d, 0.0)) - radius;
}

float4 PSMain(VSOut input) : SV_TARGET {
    // ── TEXT MODE ──
    if (input.type > 0.5) {
        float2 texUV = lerp(input.uv0, input.uv1, input.uv);
        float dist = g_tex.Sample(g_sam, texUV).r;

        // SDF edge detection — sharp, scalable text
        float edge = 0.5;
        float aa   = fwidth(dist) * 1.5;
        float alpha = smoothstep(edge - aa, edge + aa, dist);

        // Glow/shadow behind text
        if (input.glowSize > 0.0) {
            float glowDist = g_tex.Sample(g_sam, texUV + float2(-0.003, 0.003)).r;
            float glowAlpha = smoothstep(edge - aa - 0.15, edge + aa, glowDist);
            float4 glowColor = input.borderColor; // reuse borderColor as glow color
            glowColor.a *= glowAlpha * input.glowIntensity;
            // Under-blend glow
            float4 result = float4(input.color.rgb, alpha * input.color.a);
            result.rgb = lerp(glowColor.rgb, result.rgb, result.a / (result.a + glowColor.a + 0.001));
            result.a = max(result.a, glowColor.a);
            return result;
        }

        return float4(input.color.rgb, alpha * input.color.a);
    }

    // ── RECT MODE ──
    float2 p = (input.uv - 0.5) * input.pixelSize;
    float2 halfSize = input.pixelSize * 0.5;
    float radius = min(input.cornerRadius, min(halfSize.x, halfSize.y));

    // SDF distance (negative = inside)
    float dist = roundedRectSDF(p, halfSize, radius);

    // Smooth fill with 1px anti-aliasing
    float aa = 1.0;
    float fillAlpha = 1.0 - smoothstep(-aa, aa, dist);

    // Border
    float borderAlpha = 0.0;
    if (input.borderWidth > 0.0) {
        float outer = dist;
        float inner = dist + input.borderWidth;
        borderAlpha = smoothstep(-aa, aa, inner) - smoothstep(-aa, aa, outer);
        borderAlpha = clamp(borderAlpha, 0.0, 1.0);
    }

    // Glow (outside the rect)
    float glowAlpha = 0.0;
    if (input.glowSize > 0.0) {
        float glowDist = dist + input.glowSize;
        glowAlpha = 1.0 - smoothstep(-aa, aa, glowDist);
        glowAlpha *= input.glowIntensity;
    }

    // Compose
    float4 fillColor = input.color;
    float4 borderColor = input.borderColor;
    float4 glowColor = float4(borderColor.rgb, glowAlpha * 0.5);

    // Layer: glow → fill → border on top
    float4 result = float4(0, 0, 0, 0);

    // Glow layer (additive-ish)
    if (glowAlpha > 0.001) {
        result = glowColor;
    }

    // Fill layer
    if (fillAlpha > 0.001) {
        float4 fillLayer = float4(fillColor.rgb, fillColor.a * fillAlpha);
        result = result + fillLayer * (1.0 - result.a);
    }

    // Border layer
    if (borderAlpha > 0.001) {
        float4 borderLayer = float4(borderColor.rgb, borderColor.a * borderAlpha);
        result = result + borderLayer * (1.0 - result.a);
    }

    return result;
}
