#include "PianoRenderer.h"
#include <cmath>

namespace pfd {

bool PianoRenderer::Initialize(ID3D11Device* device, uint32_t viewW, uint32_t viewH) {
    m_viewW = viewW;
    m_viewH = viewH;
    m_pianoY = (float)viewH - m_pianoHeight;

    for (int i = 0; i < 16; i++)
        m_channelColors[i] = util::ChannelColor(i);

    particles.Initialize(device);
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
    // Count white keys in range A0(21)-C8(108)
    int whiteCount = 0;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++)
        if (!NoteState::IsBlackKey(n)) whiteCount++;

    m_gap = std::max(1.0f, m_viewW * 0.001f);
    m_whiteKeyW = std::max(8.0f, ((float)m_viewW - m_gap * (whiteCount - 1)) / whiteCount);
    m_blackKeyW = std::max(5.0f, m_whiteKeyW * 0.58f);

    // Position white keys
    float wx = 0;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!NoteState::IsBlackKey(n)) {
            m_keys[n] = {wx, m_whiteKeyW, false};
            wx += m_whiteKeyW + m_gap;
        }
    }

    // Position black keys centered between neighbors
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!NoteState::IsBlackKey(n)) continue;
        int left = n - 1;
        while (left >= NoteState::FIRST_KEY && NoteState::IsBlackKey(left)) left--;
        if (left >= NoteState::FIRST_KEY) {
            float center = m_keys[left].x + m_whiteKeyW;
            m_keys[n] = {center - m_blackKeyW * 0.5f, m_blackKeyW, true};
        }
    }
}

float PianoRenderer::GetKeyX(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].x;
}

float PianoRenderer::GetKeyWidth(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].width;
}

void PianoRenderer::Update(const NoteState& state, float currentTime, float dt) {
    // Trigger impact flashes for new notes
    for (int note : state.RecentNoteOns()) {
        m_impacts.push_back({note, currentTime, 0.3f});
        auto& info = state[note];
        util::Color color = m_channelColors[info.channel % 16];
        particles.EmitBurst(GetKeyX(note) + GetKeyWidth(note) * 0.5f, m_pianoY, color);
        particles.EmitSparks(GetKeyX(note) + GetKeyWidth(note) * 0.5f, m_pianoY, color);
    }

    // Continuous particles for held notes
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        auto& info = state[n];
        if (info.active) {
            util::Color color = m_channelColors[info.channel % 16];
            particles.EmitContinuous(
                m_keys[n].x + m_keys[n].width * 0.5f,
                m_pianoY,
                color
            );
        }
    }

    // Update particles
    particles.Update(dt);

    // Remove expired impacts
    m_impacts.erase(
        std::remove_if(m_impacts.begin(), m_impacts.end(),
            [&](const ImpactFlash& f) { return currentTime - f.time > f.duration; }),
        m_impacts.end()
    );
}

void PianoRenderer::Render(SpriteBatch& batch, const NoteState& state, float currentTime, float dt) {
    DrawAtmosphere(batch, state);
    DrawFallingNotes(batch, state, currentTime);
    DrawImpactFlashes(batch, currentTime);
    particles.Draw(batch);
    DrawSaber(batch, state, currentTime);
    DrawPiano(batch, state, currentTime);
}

void PianoRenderer::DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime) {
    // Draw white keys first (behind black keys)
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        auto& info = state[n];
        float x = m_keys[n].x;
        float w = m_keys[n].width;

        // Key base
        util::Vec4 color = {0.95f, 0.95f, 0.96f, 1.0f};

        // Hit flash
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            color = {1.0f, 0.6f + 0.35f * flash, 0.2f * flash + 0.8f, 1.0f};
        }

        batch.Draw({x, m_pianoY}, {w, m_pianoHeight}, color);

        // Key border
        batch.Draw({x, m_pianoY}, {w, 1.0f}, {0.0f, 0.0f, 0.0f, 0.3f});
    }

    // Draw black keys on top
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        auto& info = state[n];
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        float h = m_pianoHeight * 0.63f;

        util::Vec4 color = {0.18f, 0.18f, 0.20f, 1.0f};
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            color = {0.9f, 0.4f + 0.3f * flash, 0.1f * flash, 1.0f};
        }

        batch.Draw({x, m_pianoY}, {w, h}, color);
    }
}

void PianoRenderer::DrawFallingNotes(SpriteBatch& batch, const NoteState& state, float currentTime) {
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        auto& info = state[n];
        if (!info.visualActive) continue;

        float noteX = m_keys[n].x;
        float noteW = m_keys[n].width;
        float alpha = 1.0f;

        // Note is falling: top edge at (currentTime - onTime) * speed above piano
        float yTop, yBottom;
        float velocityScale = 0.5f + (info.velocity / 127.0f) * 0.5f;

        if (info.active) {
            // Note is held — draw from piano line upward
            yBottom = m_pianoY;
            yTop = m_pianoY - (currentTime - (float)info.onTime) * m_noteSpeed * velocityScale;
            if (yTop < 0) yTop = 0;
        } else {
            // Note released — bottom edge continues falling
            float releaseElapsed = currentTime - (float)info.offTime;
            yBottom = m_pianoY + releaseElapsed * m_noteSpeed * 0.5f;
            yTop = m_pianoY - ((float)info.offTime - (float)info.onTime) * m_noteSpeed * velocityScale;
            alpha = std::max(0.0f, 1.0f - releaseElapsed * 3.0f);
            if (alpha <= 0) {
                const_cast<NoteInfo&>(info).visualActive = false;
                continue;
            }
        }

        float noteH = yBottom - yTop;
        if (noteH < 1) continue;

        util::Color baseColor = m_channelColors[info.channel % 16];

        // Gradient: brighter at hit edge (bottom), dimmer at top
        util::Vec4 bottomC = {baseColor.r, baseColor.g, baseColor.b, alpha};
        util::Vec4 topC = {baseColor.r * 0.4f, baseColor.g * 0.4f, baseColor.b * 0.4f, alpha * 0.5f};

        // Draw as two halves for gradient effect
        float halfH = noteH * 0.5f;
        batch.Draw({noteX, yTop}, {noteW, halfH}, topC);
        batch.Draw({noteX, yTop + halfH}, {noteW, halfH}, bottomC);

        // Glow behind note (larger, dimmer)
        float glowExpand = 4.0f;
        batch.Draw(
            {noteX - glowExpand, yTop},
            {noteW + glowExpand * 2, noteH},
            {baseColor.r, baseColor.g, baseColor.b, alpha * 0.15f}
        );
    }
}

void PianoRenderer::DrawImpactFlashes(SpriteBatch& batch, float currentTime) {
    for (auto& flash : m_impacts) {
        float elapsed = currentTime - flash.time;
        float t = elapsed / flash.duration;
        if (t > 1.0f) continue;

        float x = m_keys[flash.note].x + m_keys[flash.note].width * 0.5f;
        float radius = 10.0f + t * 40.0f;
        float alpha = (1.0f - t) * 0.6f;

        // Expanding ring (approximate with 4 quads)
        batch.Draw(
            {x - radius, m_pianoY - radius},
            {radius * 2, radius * 2},
            {1.0f, 0.9f, 0.7f, alpha}
        );
    }
}

void PianoRenderer::DrawAtmosphere(SpriteBatch& batch, const NoteState& state) {
    // Subtle ambient glow at the piano line
    float glowH = 200.0f;
    batch.Draw(
        {0, m_pianoY - glowH},
        {(float)m_viewW, glowH},
        {0.12f, 0.15f, 0.25f, 0.3f}
    );
}

void PianoRenderer::DrawSaber(SpriteBatch& batch, const NoteState& state, float currentTime) {
    // Horizontal light beam at the piano line, colored by active notes
    float saberH = 3.0f;
    util::Color saberColor{0, 0, 0, 0};
    int activeCount = 0;

    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (state[n].active) {
            util::Color c = m_channelColors[state[n].channel % 16];
            saberColor.r += c.r;
            saberColor.g += c.g;
            saberColor.b += c.b;
            activeCount++;
        }
    }

    if (activeCount > 0) {
        float inv = 1.0f / activeCount;
        saberColor.r *= inv;
        saberColor.g *= inv;
        saberColor.b *= inv;

        // Center glow
        batch.Draw(
            {0, m_pianoY - saberH * 3},
            {(float)m_viewW, saberH * 7},
            {saberColor.r, saberColor.g, saberColor.b, 0.15f}
        );
        // Core line
        batch.Draw(
            {0, m_pianoY - saberH * 0.5f},
            {(float)m_viewW, saberH},
            {saberColor.r, saberColor.g, saberColor.b, 0.6f}
        );
    }
}

} // namespace pfd
