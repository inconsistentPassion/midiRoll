#include "PianoRenderer.h"
#include <algorithm>
#include <cmath>

namespace pfd {

bool PianoRenderer::Initialize(ID3D11Device* device, uint32_t viewW, uint32_t viewH) {
    m_viewW = viewW;
    m_viewH = viewH;
    m_pianoY = (float)viewH - m_pianoHeight;
    
    if (!particles.Initialize(device)) return false;

    // Initialize channel colors
    m_channelColors[0] = {0.2f, 0.5f, 1.0f, 1.0f}; // Blue
    m_channelColors[1] = {1.0f, 0.4f, 0.1f, 1.0f}; // Orange
    m_channelColors[2] = {0.1f, 0.8f, 0.2f, 1.0f}; // Green
    m_channelColors[3] = {0.8f, 0.1f, 0.8f, 1.0f}; // Purple
    m_channelColors[4] = {1.0f, 0.8f, 0.0f, 1.0f}; // Yellow
    m_channelColors[9] = {0.5f, 0.5f, 0.5f, 1.0f}; // Drums (Grey)
    for (int i = 0; i < 16; i++) {
        if (m_channelColors[i].a == 0) m_channelColors[i] = {0.5f, 0.7f, 1.0f, 1.0f};
    }

    ComputeKeyLayout();
    return true;
}

void PianoRenderer::Resize(uint32_t viewW, uint32_t viewH) {
    m_viewW = viewW;
    m_viewH = viewH;
    m_pianoY = (float)viewH - m_pianoHeight;
    ComputeKeyLayout();
}

void PianoRenderer::ComputeKeyLayout() {
    int whiteKeyCount = 0;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        int m = n % 12;
        bool isBlack = (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
        if (!isBlack) whiteKeyCount++;
    }

    float keyW = (float)m_viewW / (float)whiteKeyCount;
    float x = 0;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        int m = n % 12;
        bool isBlack = (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);

        m_keys[n].isBlack = isBlack;
        m_keys[n].width = isBlack ? keyW * 0.65f : keyW;
        
        if (isBlack) {
            m_keys[n].x = x - m_keys[n].width * 0.5f;
        } else {
            m_keys[n].x = x;
            x += keyW;
        }
    }
}

void PianoRenderer::Update(const NoteState& state, float currentTime, float dt) {
    particles.Update(dt);
    const_cast<NoteState&>(state).UpdateVisualNotes(currentTime);

    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (state[n].active) {
            float x = m_keys[n].x + m_keys[n].width * 0.5f;
            util::Color c = m_channelColors[state[n].channel % 16];
            particles.EmitContinuous(x, m_pianoY, c);
        }
    }

    m_impacts.erase(
        std::remove_if(m_impacts.begin(), m_impacts.end(),
            [&](const ImpactFlash& f) { return currentTime - f.time > f.duration; }),
        m_impacts.end()
    );
}

void PianoRenderer::Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes, float liveTime, float midiPlaybackTime, float dt) {
    DrawImpactFlashes(batch, liveTime);
    DrawAtmosphere(batch, state);
    
    batch.Draw({0, m_pianoY - 5}, {(float)m_viewW, m_pianoHeight + 10}, {0.05f, 0.05f, 0.05f, 1.0f});

    // 1. Draw MIDI notes (respect m_falling setting)
    for (const auto& note : midiNotes) {
        float noteX = m_keys[note.note].x;
        float noteW = m_keys[note.note].width;
        float yS, yE;

        if (m_falling) {
            // FALLING: Future notes drop from top to piano
            if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
            yE = m_pianoY - (float)(note.start - midiPlaybackTime) * m_noteSpeed;
            yS = m_pianoY - (float)(note.end - midiPlaybackTime) * m_noteSpeed;
            if (yS < 0) yS = 0;
            if (yE > m_pianoY) yE = m_pianoY;
        } else {
            // RISING: History notes move from piano to top
            if (note.start > midiPlaybackTime || note.end < midiPlaybackTime - 5.0) continue;
            yE = m_pianoY - (float)(midiPlaybackTime - note.start) * m_noteSpeed;
            yS = m_pianoY - (float)(midiPlaybackTime - note.end) * m_noteSpeed;
            if (yE > m_pianoY) yE = m_pianoY;
            if (yS < 0) yS = 0;
        }

        if (std::abs(yE - yS) > 1.0f) {
            util::Color c = m_channelColors[note.channel % 16];
            batch.Draw({noteX, std::min(yS, yE)}, {noteW, std::abs(yE - yS)}, {c.r, c.g, c.b, 1.0f});
        }
    }

    // 2. Draw LIVE notes (history, respect m_falling setting)
    for (const auto& vn : state.GetVisualNotes()) {
        float noteX = m_keys[vn.note].x;
        float noteW = m_keys[vn.note].width;
        float yT, yB;

        if (m_falling) {
            // In Falling mode, history moves DOWN past the piano (off-screen)
            if (vn.active) {
                yT = m_pianoY;
                yB = m_pianoY + (liveTime - (float)vn.onTime) * m_noteSpeed;
            } else {
                float d = (float)vn.offTime - (float)vn.onTime;
                float r = liveTime - (float)vn.offTime;
                yT = m_pianoY + r * m_noteSpeed;
                yB = m_pianoY + (d + r) * m_noteSpeed;
                if (yT > (float)m_viewH) continue;
            }
        } else {
            // In Rising mode, history moves UP away from piano
            if (vn.active) {
                yB = m_pianoY;
                yT = m_pianoY - (liveTime - (float)vn.onTime) * m_noteSpeed;
            } else {
                float d = (float)vn.offTime - (float)vn.onTime;
                float r = liveTime - (float)vn.offTime;
                yB = m_pianoY - r * m_noteSpeed;
                yT = m_pianoY - (d + r) * m_noteSpeed;
                if (yB < 0) continue;
            }
        }
        util::Color c = m_channelColors[vn.channel % 16];
        batch.Draw({noteX, std::min(yT, yB)}, {noteW, std::abs(yB - yT)}, {c.r, c.g, c.b, 1.0f});
    }

    DrawPiano(batch, state, liveTime);
    DrawSaber(batch, state, liveTime);
    particles.Draw(batch);
}

void PianoRenderer::DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime) {
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        auto& info = state[n];
        float x = m_keys[n].x + 1;
        float w = m_keys[n].width - 2;
        util::Vec4 color = {0.95f, 0.95f, 0.95f, 1.0f};
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            color = {0.8f, 0.5f + 0.3f * flash, 0.2f * flash, 1.0f};
        }
        batch.Draw({x, m_pianoY}, {w, m_pianoHeight}, color);
        batch.Draw({x, m_pianoY + m_pianoHeight - 4}, {w, 4}, {0, 0, 0, 0.15f});
    }
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        auto& info = state[n];
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        float h = m_pianoHeight * 0.63f;
        util::Vec4 color = {0.12f, 0.12f, 0.14f, 1.0f};
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            color = {0.7f, 0.4f + 0.3f * flash, 0.2f * flash, 1.0f};
        }
        batch.Draw({x, m_pianoY}, {w, h}, color);
        batch.Draw({x + 2, m_pianoY}, {w - 4, 3}, {1, 1, 1, 0.1f});
    }
}

void PianoRenderer::DrawNotes(SpriteBatch&, const NoteState&, float) {}

float PianoRenderer::GetKeyX(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].x;
}

float PianoRenderer::GetKeyWidth(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].width;
}

void PianoRenderer::DrawImpactFlashes(SpriteBatch& batch, float currentTime) {
    for (auto& flash : m_impacts) {
        float elapsed = currentTime - flash.time;
        float t = elapsed / flash.duration;
        if (t > 1.0f) continue;
        float x = m_keys[flash.note].x + m_keys[flash.note].width * 0.5f;
        float radius = 10.0f + t * 40.0f;
        batch.Draw({x - radius, m_pianoY - radius}, {radius * 2, radius * 2}, {1, 0.9f, 0.7f, (1.0f - t) * 0.5f});
    }
}

void PianoRenderer::DrawAtmosphere(SpriteBatch& batch, const NoteState&) {
    batch.Draw({0, m_pianoY - 100}, {(float)m_viewW, 100}, {0.1f, 0.12f, 0.2f, 0.2f});
}

void PianoRenderer::DrawSaber(SpriteBatch& batch, const NoteState& state, float) {
    util::Color sc{0, 0, 0, 0};
    int count = 0;
    for (int i = 0; i < 128; i++) {
        if (state[i].active) {
            util::Color c = m_channelColors[state[i].channel % 16];
            sc.r += c.r; sc.g += c.g; sc.b += c.b; count++;
        }
    }
    if (count > 0) {
        sc.r /= count; sc.g /= count; sc.b /= count;
        batch.Draw({0, m_pianoY - 10}, {(float)m_viewW, 20}, {sc.r, sc.g, sc.b, 0.1f});
        batch.Draw({0, m_pianoY - 1}, {(float)m_viewW, 2}, {sc.r, sc.g, sc.b, 0.7f});
    }
}

} // namespace pfd
