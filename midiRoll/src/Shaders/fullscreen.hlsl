// fullscreen.hlsl — Fullscreen triangle vertex shader + post-processing
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VERTEXID) {
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * 2 - 1, 0, 1);
    o.pos.y = -o.pos.y;
    return o;
}

Texture2D    sceneTex : register(t0);
Texture2D    bloomTex : register(t1);
SamplerState sam      : register(s0);

float4 PSComposite(VSOut input) : SV_TARGET {
    float4 scene = sceneTex.Sample(sam, input.uv);
    float4 bloom = bloomTex.Sample(sam, input.uv);
    return scene + bloom;
}
