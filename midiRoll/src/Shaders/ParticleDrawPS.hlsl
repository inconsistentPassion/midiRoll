// ParticleDrawPS.hlsl — Pixel shader for GPU particle rendering

Texture2D    tex : register(t0);
SamplerState sam : register(s0);

struct PSIn {
    float4 pos   : SV_POSITION;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

float4 PSMain(PSIn input) : SV_TARGET {
    float4 texColor = tex.Sample(sam, input.uv);
    // Same as SpriteBatch: red channel as alpha mask (works for R8 glow textures)
    return float4(input.color.rgb, input.color.a * texColor.r);
}
