#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "NoteState.h"
#include "GPUNoteSystem.h"
#include "../Audio/MidiParser.h"
#include "../Effects/GPUParticleSystem.h" 
#include "../Util/Color.h"
#include <array>

namespace pfd {

struct ImpactFlash {
    int   note;
    float time;      // when it happened
    float duration;
};

class PianoRenderer {
public:
    bool Initialize(ID3D11Device* device, uint32_t viewW, uint32_t viewH);
    void Resize(uint32_t viewW, uint32_t viewH);
    void Update(NoteState& state, float currentTime, float dt);
    void Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes,
                float liveTime, float midiPlaybackTime, float dt,
                ID3D11DeviceContext* d3dCtx);  // Added d3dCtx for GPU particles

    void SetNoteSpeed(float s) { m_noteSpeed = s; }
    float GetNoteSpeed() const { return m_noteSpeed; }
    
    void SetSaberColor(int idx) { m_saberColorIdx = idx; }

    ID3D11ShaderResourceView* GetNoteTex() const { return m_noteTex.Get(); }
    ID3D11ShaderResourceView* GetGradientTex() const { return m_gradientTex.Get(); }
    void SetChannelColor(int ch, util::Color c) { m_channelColors[ch] = c; }

    // Falling/Rising mode
    void SetFalling(bool falling) { m_falling = falling; }
    bool IsFalling() const { return m_falling; }
    void ToggleDirection() { m_falling = !m_falling; }

    // For particle effects
    float GetKeyX(int note) const;
    float GetKeyWidth(int note) const;
    bool  IsBlack(int note) const { return m_keys[note].isBlack; }
    util::Color GetChannelColor(int ch) const { return m_channelColors[ch % 64]; }
    float GetPianoY() const { return m_pianoY; }
    float GetPianoHeight() const { return m_pianoHeight; }

    void UpdateGpuNotes(ID3D11Device* device, const std::vector<Note>& notes);

    GPUParticleSystem particles;
    GPUNoteSystem     gpuNotes; 
private:
    void ComputeKeyLayout();
    void DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime);
    void DrawImpactFlashes(SpriteBatch& batch, float currentTime);
    void DrawAtmosphere(SpriteBatch& batch, const NoteState& state);
    void DrawSaber(SpriteBatch& batch, const NoteState& state, float currentTime);

    // Spatial culling helpers
    bool IsMidiNoteVisible(const Note& note, float midiPlaybackTime, float& outY, float& outH) const;
    bool IsLiveNoteVisible(const ActiveVisualNote& vn, float liveTime, float& outY, float& outH) const;

    // Key layout
    struct KeyInfo {
        float x;
        float width;
        bool  isBlack;
    };
    std::array<KeyInfo, 128> m_keys{};

    uint32_t m_viewW{}, m_viewH{};
    float m_pianoHeight = 140.0f;
    float m_pianoY{};
    float m_noteSpeed = 400.0f; // pixels per second
    bool  m_falling = false;     // true = notes fall from top, false = notes rise from piano
    float m_whiteKeyW{};
    float m_blackKeyW{};
    float m_gap = 1.0f;

    std::array<util::Color, 64> m_channelColors{};
    std::vector<ImpactFlash> m_impacts;
    ComPtr<ID3D11ShaderResourceView> m_noteTex;
    ComPtr<ID3D11ShaderResourceView> m_gradientTex;
    ComPtr<ID3D11ShaderResourceView> m_glowTex;
    ComPtr<ID3D11ShaderResourceView> m_noiseTex;
    int m_saberColorIdx = 15; // default white
    void CreateTextures(ID3D11Device* device);
};

} // namespace pfd
