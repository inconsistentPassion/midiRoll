// note.hlsl — Falling note rendering
// Placeholder for external shader file. Currently handled by inline SpriteBatch shaders.

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

VSOut VSMain(float2 pos : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0) {
    VSOut o;
    o.pos = float4(pos, 0, 1);
    o.uv  = uv;
    o.col = color;
    return o;
}

float4 PSMain(VSOut input) : SV_TARGET {
    return input.col;
}
