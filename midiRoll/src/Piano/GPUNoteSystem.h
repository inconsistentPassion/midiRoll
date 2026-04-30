#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "../Piano/NoteState.h"
#include "../Audio/MidiParser.h"
#include <vector>

namespace pfd {

// GPU-friendly note structure
struct GPUNote {
    float start;
    float end;
    uint32_t noteNumber;
    uint32_t channel;
    float velocity;
    uint32_t flags; // bit 0: isBlack, bit 1: isActive
};

class GPUNoteSystem {
public:
    struct KeyLayout {
        float x;
        float width;
        float isBlack;
        float pad;
    };

    bool Initialize(ID3D11Device* device);
    void UpdateMidiNotes(ID3D11Device* device, const std::vector<Note>& notes);
    
    void Render(ID3D11DeviceContext* ctx, 
                const std::vector<ActiveVisualNote>& liveNotes,
                const std::array<util::Color, 64>& channelColors,
                const std::array<KeyLayout, 128>& layout,
                float currentTime, 
                float noteSpeed, 
                float pianoY,
                float viewW,
                float viewH,
                bool falling);

private:
    void CreateResources(ID3D11Device* device);
    void CreateShaders(ID3D11Device* device);

    // Note buffers
    ComPtr<ID3D11Buffer>              m_midiNoteBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_midiNoteSRV;
    uint32_t                          m_midiNoteCount = 0;

    // Live note buffer (re-uploaded every frame)
    ComPtr<ID3D11Buffer>              m_liveNoteBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_liveNoteSRV;
    uint32_t                          m_liveNoteCapacity = 4096;

    ComPtr<ID3D11Buffer>              m_layoutBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_layoutSRV;

    // Constant buffer for rendering parameters
    struct CBNoteParams {
        float currentTime;
        float noteSpeed;
        float pianoY;
        float viewW;
        float viewH;
        float falling;
        float pad[2];
        util::Vec4 channelColors[64];
    };
    ComPtr<ID3D11Buffer>              m_cbParams;

    // Shaders
    ComPtr<ID3D11VertexShader>        m_vs;
    ComPtr<ID3D11PixelShader>         m_ps;
    ComPtr<ID3D11InputLayout>         m_layout;
    
    // States
    ComPtr<ID3D11BlendState>          m_blendState;
    ComPtr<ID3D11RasterizerState>     m_rasterizer;
    ComPtr<ID3D11DepthStencilState>   m_depthStencil;

    // Geometry (instanced quad)
    ComPtr<ID3D11Buffer>              m_quadVB;
    ComPtr<ID3D11Buffer>              m_quadIB;
};

} // namespace pfd
