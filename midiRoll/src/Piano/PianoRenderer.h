#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "NoteState.h"
#include "../Audio/MidiParser.h"
#include "../Effects/ParticleSystem.h"
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
    void Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes, float liveTime, float midiPlaybackTime, float dt);

    void SetNoteSpeed(float s) { m_noteSpeed = s; }
    float GetNoteSpeed() const { return m_noteSpeed; }

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
    float GetPianoY() const { return m_pianoY; }
    float GetPianoHeight() const { return m_pianoHeight; }

    ParticleSystem particles;

private:
    void ComputeKeyLayout();
    void DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime);
    void DrawImpactFlashes(SpriteBatch& batch, float currentTime);
    void DrawAtmosphere(SpriteBatch& batch, const NoteState& state);
    void DrawSaber(SpriteBatch& batch, const NoteState& state, float currentTime);

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
    void CreateTextures(ID3D11Device* device);
};

} // namespace pfd
