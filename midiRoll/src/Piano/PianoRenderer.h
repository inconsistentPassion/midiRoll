#pragma once
#include "../Renderer/D3DContext.h"
#include "../Renderer/SpriteBatch.h"
#include "../Renderer/RenderTarget.h"
#include "NoteState.h"
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
    void Update(const NoteState& state, float currentTime, float dt);
    void Render(SpriteBatch& batch, const NoteState& state, float currentTime, float dt);

    // Settings
    void SetNoteSpeed(float pxPerSec) { m_noteSpeed = pxPerSec; }
    void SetPianoHeight(float h)      { m_pianoHeight = h; }
    void SetChannelColor(int ch, util::Color c) { m_channelColors[ch] = c; }

    // For particle effects
    float GetKeyX(int note) const;
    float GetKeyWidth(int note) const;
    float GetPianoY() const { return m_pianoY; }

    ParticleSystem particles;

private:
    void ComputeKeyLayout();
    void DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime);
    void DrawFallingNotes(SpriteBatch& batch, const NoteState& state, float currentTime);
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
    float m_whiteKeyW{};
    float m_blackKeyW{};
    float m_gap = 1.0f;

    std::array<util::Color, 16> m_channelColors{};
    std::vector<ImpactFlash> m_impacts;
};

} // namespace pfd
