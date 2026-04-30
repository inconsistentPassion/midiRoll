#include "GPUNoteSystem.h"
#include <d3dcompiler.h>
#include <algorithm>

namespace pfd {

// ─────────────────────────────────────────────────────────────────────────────
// HLSL — Vertex Shader
// Calculates position, size, and clips notes to the waterfall area
// ─────────────────────────────────────────────────────────────────────────────
static const char* g_vsNoteSource = R"(
struct NoteData {
    float start;
    float end;
    uint  noteNumber;
    uint  channel;
    float velocity;
    uint  flags; 
};

struct KeyLayout {
    float x;
    float width;
    float isBlack;
    float pad;
};

StructuredBuffer<NoteData>  g_notes  : register(t0);
StructuredBuffer<KeyLayout> g_layout : register(t1);

cbuffer CBParams : register(b0) {
    float  g_time;
    float  g_speed;
    float  g_pianoY;
    float  g_viewW;
    float  g_viewH;
    float  g_falling;
    float2 g_pad;
    float4 g_colors[64];
};

struct VSOut {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float4 color  : COLOR0;
    float2 size   : TEXCOORD1;
    uint   flags  : BLENDINDICES0;
    float  seed   : TEXCOORD3;
};

VSOut Degenerate(float2 uv) {
    VSOut o;
    o.pos = float4(0,0,0,1);
    o.uv = uv;
    o.color = float4(0,0,0,0);
    o.size = float2(0,0);
    o.flags = 0;
    o.seed = 0;
    return o;
}

VSOut VSMain(float2 vPos : POSITION, float2 vUV : TEXCOORD0, uint instID : SV_InstanceID) {
    NoteData n = g_notes[instID];
    KeyLayout k = g_layout[n.noteNumber % 128];

    float yS, yE;
    bool isActive = (n.flags & 2u) != 0;
    bool isMidi   = (n.flags & 4u) != 0;

    if (g_falling > 0.5) {
        if (isMidi) {
            // GHOST NOTE FIX: If the note has already finished playing, don't draw it
            if (g_time > n.end) return Degenerate(vUV);
            
            yE = g_pianoY - (n.start - g_time) * g_speed;
            yS = g_pianoY - (n.end   - g_time) * g_speed;
            if (yE > g_pianoY) yE = g_pianoY;
        } else {
            if (isActive) {
                yE = g_pianoY;
                yS = g_pianoY + (g_time - n.start) * g_speed;
            } else {
                float d = n.end - n.start;
                float r = g_time - n.end;
                yE = g_pianoY + r * g_speed;
                yS = g_pianoY + (d + r) * g_speed;
            }
        }
        if (yE > g_viewH) return Degenerate(vUV);
    } else {
        // Rising mode
        if (isMidi) {
            // GHOST NOTE FIX: If the note has already finished playing, don't draw it
            if (g_time > n.end) return Degenerate(vUV);

            yE = g_pianoY + (n.start - g_time) * g_speed;
            yS = g_pianoY + (n.end   - g_time) * g_speed;
            if (yE < g_pianoY) yE = g_pianoY; // Clip at piano line
        } else {
            if (isActive) {
                yE = g_pianoY;
                yS = g_pianoY - (g_time - n.start) * g_speed;
            } else {
                float d = n.end - n.start;
                float r = g_time - n.end;
                yE = g_pianoY - r * g_speed;
                yS = g_pianoY - (d + r) * g_speed;
            }
        }
        if (yE < 0) return Degenerate(vUV);
    }

    float h = abs(yE - yS);
    float y = min(yS, yE);
    float w = k.width - 2.0;
    float x = k.x + 1.0;

    if (h < 0.5 || y > g_viewH || (y + h) < 0) return Degenerate(vUV);

    // INFLATE QUAD FOR BLOOM: Add padding so bloom doesn't hit sharp edges
    float padding = 20.0;
    float2 quadSize = float2(w, h) + padding * 2.0;
    float2 quadOffset = float2(-padding, -padding);
    
    float2 worldPos = float2(x, y) + quadOffset + vPos * quadSize;
    float2 ndc;
    ndc.x = (worldPos.x / g_viewW) * 2.0 - 1.0;
    ndc.y = 1.0 - (worldPos.y / g_viewH) * 2.0;

    VSOut o;
    o.pos = float4(ndc, 0, 1);
    
    // Re-map UV so the shader knows where the actual note is
    // vUV goes 0..1 over the inflated quad
    // We want the shader to see -padding..w+padding
    o.uv = (vPos * quadSize + quadOffset) / float2(w, h); 
    
    float4 c = g_colors[n.channel % 64];
    if (k.isBlack > 0.5) c.rgb *= 0.75;
    o.color = c;
    o.size = float2(w, h);
    o.flags = n.flags;
    
    // Seed stability + Epsilon
    o.seed = frac((n.start + 0.001) * 123.456) + (float)n.noteNumber * 0.131;
    return o;
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// HLSL — Pixel Shader
// Procedural rounded corners, caustics, and glow
// ─────────────────────────────────────────────────────────────────────────────
static const char* g_psNoteSource = R"(
cbuffer CBParams : register(b0) {
    float  g_time;
    float  g_speed;
    float  g_pianoY;
    float  g_viewW;
    float  g_viewH;
    float  g_falling;
    float2 g_pad;
    float4 g_colors[64];
};

struct PSIn {
    float4 pos    : SV_POSITION;
    float2 uv     : TEXCOORD0;
    float4 color  : COLOR0;
    float2 size   : TEXCOORD1;
    uint   flags  : BLENDINDICES0;
    float  seed   : TEXCOORD3;
};

float RoundedRectSDF(float2 p, float2 b, float r) {
    float2 d = abs(p) - b + r;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

float Hash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

float4 PSMain(PSIn input) : SV_TARGET {
    float2 size = input.size;
    float2 p = (input.uv - 0.5) * size;
    
    float radius = min(size.x * 0.48, 12.0);
    float dist = RoundedRectSDF(p, size * 0.5, radius);
    
    // NO HARD DISCARD: Let the bloom fade out naturally
    // We inflated the quad in VSMain so we have room.
    
    float2 uv = input.uv;
    float t = g_time * 1.5 + input.seed * 10.0;
    bool isActive = (input.flags & 2u) != 0;

    // 1. Atmosphere & Lighting
    float screenY = input.pos.y;
    float distFromPiano = abs(screenY - g_pianoY);
    float atmos = saturate(1.0 - distFromPiano / (g_viewH * 0.85));
    float brightness = lerp(0.4, 1.15, pow(atmos, 0.7));
    float saturation = lerp(0.5, 1.0, atmos);

    // 2. Glass Material Effects
    float fresnel = pow(1.0 - saturate(abs(uv.x - 0.5) * 2.0), 2.5);
    float glint = smoothstep(0.08, 0.0, abs(uv.x - 0.5 + sin(t * 0.2 + uv.y * 0.5) * 0.2)) * 0.35;
    
    float noise = 0.0;
    float2 nUV = uv * size * 0.06 + input.seed; 
    noise += sin(nUV.x * 0.7 + t * 0.5) * cos(nUV.y * 0.8 + t);
    noise += sin(nUV.y * 1.2 - t * 0.3) * cos(nUV.x * 0.5 + t * 0.8) * 0.5;
    noise = pow(abs(noise), 2.0);

    // 3. Color Composition
    float3 baseColor = input.color.rgb;
    float grey = dot(baseColor, float3(0.299, 0.587, 0.114));
    baseColor = lerp(float3(grey, grey, grey), baseColor, saturation);
    
    float3 finalRGB = baseColor * brightness;
    finalRGB += baseColor * noise * 0.4 * brightness;
    finalRGB += (baseColor + 0.5) * fresnel * 0.25 * brightness;
    finalRGB += float3(1,1,1) * glint * brightness;

    // 4. Edges
    float rim = smoothstep(2.0, 0.0, abs(dist + 1.0)) * 0.6;
    finalRGB += (baseColor + 0.5) * rim * brightness;

    finalRGB = min(finalRGB, 1.1); // Allow a bit over 1.0 for bloom pick-up

    // 5. EXPANSIVE BLOOM
    float alpha = smoothstep(1.5, 0.0, dist);
    
    // Bloom needs to fade to exactly 0 at the quad edge (20px padding)
    float bloom = saturate(1.0 - dist / 18.0); 
    bloom = pow(bloom, 2.0) * 0.5; // Removed atmos
    
    float finalAlpha = (alpha + bloom) * input.color.a;
    if (finalAlpha < 0.001) discard;

    return float4(finalRGB, finalAlpha);
}
)";

// ─────────────────────────────────────────────────────────────────────────────
// C++ Implementation
// ─────────────────────────────────────────────────────────────────────────────

static ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target, 0, 0, blob.GetAddressOf(), err.GetAddressOf());
    if (err) OutputDebugStringA((const char*)err->GetBufferPointer());
    return blob;
}

bool GPUNoteSystem::Initialize(ID3D11Device* device) {
    CreateResources(device);
    CreateShaders(device);
    return true;
}

void GPUNoteSystem::CreateResources(ID3D11Device* device) {
    // Constant buffer
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(CBNoteParams);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&bd, nullptr, m_cbParams.GetAddressOf());

    // Layout buffer (128 keys)
    bd.ByteWidth = sizeof(KeyLayout) * 128;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(KeyLayout);
    device->CreateBuffer(&bd, nullptr, m_layoutBuffer.GetAddressOf());
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = 128;
    device->CreateShaderResourceView(m_layoutBuffer.Get(), &srv, m_layoutSRV.GetAddressOf());

    // Live Note Buffer
    bd.ByteWidth = sizeof(GPUNote) * m_liveNoteCapacity;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(GPUNote);
    device->CreateBuffer(&bd, nullptr, m_liveNoteBuffer.GetAddressOf());

    srv.Buffer.NumElements = m_liveNoteCapacity;
    device->CreateShaderResourceView(m_liveNoteBuffer.Get(), &srv, m_liveNoteSRV.GetAddressOf());

    // Quad geometry
    struct V { float x, y, u, v; };
    V verts[] = {{0,0,0,0},{1,0,1,0},{1,1,1,1},{0,1,0,1}};
    bd.ByteWidth = sizeof(verts);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.MiscFlags = 0; bd.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = verts;
    device->CreateBuffer(&bd, &init, m_quadVB.GetAddressOf());

    uint16_t idx[] = {0,1,2,0,2,3};
    bd.ByteWidth = sizeof(idx);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    init.pSysMem = idx;
    device->CreateBuffer(&bd, &init, m_quadIB.GetAddressOf());

    // States
    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; 
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bl, m_blendState.GetAddressOf());

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    device->CreateRasterizerState(&rd, m_rasterizer.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = FALSE;
    device->CreateDepthStencilState(&dsd, m_depthStencil.GetAddressOf());
}

void GPUNoteSystem::CreateShaders(ID3D11Device* device) {
    auto vs = Compile(g_vsNoteSource, "VSMain", "vs_5_0");
    if (vs) {
        device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_vs.GetAddressOf());
        D3D11_INPUT_ELEMENT_DESC lay[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        device->CreateInputLayout(lay, 2, vs->GetBufferPointer(), vs->GetBufferSize(), m_layout.GetAddressOf());
    }

    auto ps = Compile(g_psNoteSource, "PSMain", "ps_5_0");
    if (ps) device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_ps.GetAddressOf());
}

void GPUNoteSystem::UpdateMidiNotes(ID3D11Device* device, const std::vector<Note>& notes) {
    m_midiNoteCount = (uint32_t)notes.size();
    if (m_midiNoteCount == 0) return;

    std::vector<GPUNote> gpuData;
    gpuData.reserve(m_midiNoteCount);
    for (const auto& n : notes) {
        GPUNote gn{};
        gn.start = (float)n.start;
        gn.end = (float)n.end;
        gn.noteNumber = (uint32_t)n.note;
        gn.channel = (uint32_t)n.channel;
        gn.velocity = (float)n.velocity / 127.0f;
        gn.flags = 4u; // Bit 2 = isMidi
        gpuData.push_back(gn);
    }

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = (UINT)(gpuData.size() * sizeof(GPUNote));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(GPUNote);
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = gpuData.data();
    
    m_midiNoteBuffer.Reset();
    m_midiNoteSRV.Reset();
    device->CreateBuffer(&bd, &init, m_midiNoteBuffer.GetAddressOf());
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = m_midiNoteCount;
    device->CreateShaderResourceView(m_midiNoteBuffer.Get(), &srv, m_midiNoteSRV.GetAddressOf());
}

void GPUNoteSystem::Render(ID3D11DeviceContext* ctx, 
                           const std::vector<ActiveVisualNote>& liveNotes,
                           const std::array<util::Color, 64>& channelColors,
                           const std::array<KeyLayout, 128>& layoutData,
                           float currentTime, float noteSpeed, float pianoY, float viewW, float viewH, bool falling) {
    if (m_midiNoteCount == 0 && liveNotes.empty()) return;

    // 1. Update Layout SRV
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_layoutBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, layoutData.data(), sizeof(KeyLayout) * 128);
            ctx->Unmap(m_layoutBuffer.Get(), 0);
        }
    }

    // 2. Update Constant Buffer
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cbParams.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            CBNoteParams* p = (CBNoteParams*)m.pData;
            p->currentTime = currentTime;
            p->noteSpeed = noteSpeed;
            p->pianoY = pianoY;
            p->viewW = viewW;
            p->viewH = viewH;
            p->falling = falling ? 1.0f : 0.0f;
            for(int i=0; i<64; i++) {
                p->channelColors[i] = {channelColors[i].r, channelColors[i].g, channelColors[i].b, 1.0f};
            }
            ctx->Unmap(m_cbParams.Get(), 0);
        }
    }

    // 3. Update Live Notes if any
    uint32_t liveCount = (uint32_t)liveNotes.size();
    if (liveCount > 0) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (m_liveNoteBuffer && SUCCEEDED(ctx->Map(m_liveNoteBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            GPUNote* gpuNotesArr = (GPUNote*)m.pData;
            for (uint32_t i = 0; i < std::min(liveCount, m_liveNoteCapacity); i++) {
                const auto& ln = liveNotes[i];
                uint32_t clampedNote = (uint32_t)std::clamp(ln.note, 0, 127);
                gpuNotesArr[i].start = (float)ln.onTime;
                gpuNotesArr[i].end = (float)ln.offTime;
                gpuNotesArr[i].noteNumber = clampedNote;
                gpuNotesArr[i].channel = (uint32_t)ln.channel;
                gpuNotesArr[i].velocity = (float)ln.velocity / 127.0f;
                // Bit 0: isBlack, Bit 1: isActive
                gpuNotesArr[i].flags = (layoutData[clampedNote].isBlack > 0.5f ? 1u : 0u) | (ln.active ? 2u : 0u);
            }
            ctx->Unmap(m_liveNoteBuffer.Get(), 0);
        }
    }

    // 4. Set Pipeline
    ctx->IASetInputLayout(m_layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = 16, offset = 0;
    ctx->IASetVertexBuffers(0, 1, m_quadVB.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(m_quadIB.Get(), DXGI_FORMAT_R16_UINT, 0);

    ctx->VSSetShader(m_vs.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, m_cbParams.GetAddressOf());
    ctx->VSSetShaderResources(1, 1, m_layoutSRV.GetAddressOf());

    ctx->PSSetShader(m_ps.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, m_cbParams.GetAddressOf());

    float bf[4] = {1,1,1,1};
    ctx->OMSetBlendState(m_blendState.Get(), bf, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthStencil.Get(), 0);
    ctx->RSSetState(m_rasterizer.Get());

    // 5. Draw MIDI Notes
    if (m_midiNoteCount > 0) {
        ctx->VSSetShaderResources(0, 1, m_midiNoteSRV.GetAddressOf());
        ctx->DrawIndexedInstanced(6, m_midiNoteCount, 0, 0, 0);
    }

    // 6. Draw Live Notes
    if (liveCount > 0) {
        ctx->VSSetShaderResources(0, 1, m_liveNoteSRV.GetAddressOf());
        ctx->DrawIndexedInstanced(6, std::min(liveCount, m_liveNoteCapacity), 0, 0, 0);
    }

    // Unbind
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
}

} // namespace pfd
