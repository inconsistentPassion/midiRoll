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

    CreateTextures(device);

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
    
    // Set a realistic aspect ratio (approx 4.5:1 height to width)
    m_pianoHeight = keyW * 4.5f;
    // Cap height so it doesn't take over more than 30% of the screen on very wide layouts
    if (m_pianoHeight > m_viewH * 0.3f) m_pianoHeight = m_viewH * 0.3f;
    
    m_pianoY = (float)m_viewH - m_pianoHeight;

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

void PianoRenderer::Update(NoteState& state, float currentTime, float dt) {
    particles.Update(dt);
    state.UpdateVisualNotes(currentTime);

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

void PianoRenderer::CreateTextures(ID3D11Device* device) {
    // 1. Note Texture (Rounded Box)
    const int TEX_SIZE = 64;
    std::vector<uint32_t> pixels(TEX_SIZE * TEX_SIZE);

    float radius = 12.0f;
    float border = 3.0f;
    float center = TEX_SIZE / 2.0f;

    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            float dx = std::max(0.0f, std::abs(x - center) - (center - radius));
            float dy = std::max(0.0f, std::abs(y - center) - (center - radius));
            float dist = std::sqrt(dx*dx + dy*dy);
            
            float alpha = 1.0f;
            if (dist > radius) alpha = 0.0f;
            else if (dist > radius - 1.0f) alpha = radius - dist;
            
            float intensity = 0.5f;
            if (dist >= radius - border) {
                intensity = 1.0f;
            }

            // SpriteBatch PS uses the RED channel as the final alpha mask.
            // By packing (intensity * alpha) into the RGB channels, we get a glowing border (1.0 alpha)
            // and a semi-transparent core (0.5 alpha), while respecting the rounded corners (0.0 alpha).
            uint8_t a = (uint8_t)(intensity * alpha * 255.0f);
            pixels[y * TEX_SIZE + x] = (255 << 24) | (a << 16) | (a << 8) | a;
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = TEX_SIZE;
    desc.Height = TEX_SIZE;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = TEX_SIZE * 4;

    ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, tex.GetAddressOf()))) {
        device->CreateShaderResourceView(tex.Get(), nullptr, m_noteTex.GetAddressOf());
    }

    // 2. Vertical Gradient Texture
    std::vector<uint32_t> gradPixels(64);
    for (int y = 0; y < 64; y++) {
        float alpha = 1.0f - (float)y / 63.0f; // 1.0 at top, 0.0 at bottom
        uint8_t a = (uint8_t)(alpha * 255.0f);
        gradPixels[y] = (255 << 24) | (a << 16) | (a << 8) | a;
    }
    
    desc.Width = 1;
    desc.Height = 64;
    initData.pSysMem = gradPixels.data();
    initData.SysMemPitch = 4;
    ComPtr<ID3D11Texture2D> gradTex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, gradTex.GetAddressOf()))) {
        device->CreateShaderResourceView(gradTex.Get(), nullptr, m_gradientTex.GetAddressOf());
    }
}

void PianoRenderer::Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes, float liveTime, float midiPlaybackTime, float dt) {
    (void)dt;
    DrawImpactFlashes(batch, liveTime);
    DrawAtmosphere(batch, state);
    
    batch.Draw({0, m_pianoY - 5}, {(float)m_viewW, m_pianoHeight + 10}, {0.05f, 0.05f, 0.05f, 1.0f});

    // Use rounded texture for notes
    batch.SetTexture(m_noteTex.Get());

    // Helper to draw a 3-slice rounded note so corners don't stretch vertically
    auto drawRoundedNote = [&](float x, float y, float w, float h, util::Color c) {
        if (h <= 0) return;
        float capH = w * 0.5f; // Maintain 1:1 aspect ratio for the 64x32 texture cap
        if (h < capH * 2.0f) capH = h * 0.5f; // Squish if note is extremely short
        float midH = h - capH * 2.0f;
        
        util::Vec4 color = {c.r, c.g, c.b, 1.0f};
        // Top Cap
        batch.Draw({x, y}, {w, capH}, color, {0.0f, 0.0f}, {1.0f, 0.5f});
        // Middle Body
        if (midH > 0) batch.Draw({x, y + capH}, {w, midH}, color, {0.0f, 0.49f}, {1.0f, 0.02f});
        // Bottom Cap
        batch.Draw({x, y + capH + midH}, {w, capH}, color, {0.0f, 0.5f}, {1.0f, 0.5f});
    };

    // 1. Draw MIDI notes (Future parts)
    for (const auto& note : midiNotes) {
        float noteX = m_keys[note.note].x + 1;
        float noteW = m_keys[note.note].width - 2;
        float yS, yE;

        if (m_falling) {
            // Draw part of note that hasn't hit the piano yet (above pianoY)
            if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
            yE = m_pianoY - (float)(note.start - midiPlaybackTime) * m_noteSpeed;
            yS = m_pianoY - (float)(note.end - midiPlaybackTime) * m_noteSpeed;
            
            if (yE > m_pianoY) yE = m_pianoY; // Only draw above piano
            if (yS < 0) yS = 0;
        } else {
            // Draw part of note that hasn't hit the piano yet (below pianoY)
            if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
            yE = m_pianoY + (float)(note.start - midiPlaybackTime) * m_noteSpeed;
            yS = m_pianoY + (float)(note.end - midiPlaybackTime) * m_noteSpeed;
            
            if (yE < m_pianoY) yE = m_pianoY; // Only draw below piano
            if (yS > (float)m_viewH) yS = (float)m_viewH;
        }

        if (std::abs(yE - yS) > 1.0f) {
            util::Color c = m_channelColors[note.channel % 16];
            drawRoundedNote(noteX, std::min(yS, yE), noteW, std::abs(yE - yS), c);
        }
    }

    // 2. Draw LIVE notes (history, respect m_falling setting)
    for (const auto& vn : state.GetVisualNotes()) {
        float noteX = m_keys[vn.note].x + 1;
        float noteW = m_keys[vn.note].width - 2;
        float yT, yB;

        if (m_falling) {
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
        drawRoundedNote(noteX, std::min(yT, yB), noteW, std::abs(yB - yT), c);
        
        // Emit particle trails from the trailing edge
        if (vn.active) {
            float tailY = m_falling ? yT : yB;
            particles.EmitContinuous(noteX + noteW * 0.5f, tailY, c);
        }
    }

    // Restore default texture
    batch.SetTexture(nullptr);

    DrawPiano(batch, state, liveTime);
    DrawSaber(batch, state, liveTime);
    particles.Draw(batch);
}

void PianoRenderer::DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime) {
    // White keys base
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        float x = m_keys[n].x + 1;
        float w = m_keys[n].width - 2;
        batch.Draw({x, m_pianoY}, {w, m_pianoHeight}, {0.95f, 0.95f, 0.95f, 1.0f});
        batch.Draw({x, m_pianoY + m_pianoHeight - 4}, {w, 4}, {0, 0, 0, 0.15f});
    }

    // Black keys base
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        float h = m_pianoHeight * 0.63f;
        batch.Draw({x, m_pianoY}, {w, h}, {0.12f, 0.12f, 0.14f, 1.0f});
        batch.Draw({x + 2, m_pianoY}, {w - 4, 3}, {1, 1, 1, 0.1f});
    }

    // Gradient highlights for active keys
    batch.SetBlendMode(true); // Additive blending for vivid glow
    batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        auto& info = state[n];
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            if (flash > 0) {
                float x = m_keys[n].x + (m_keys[n].isBlack ? 0 : 1);
                float w = m_keys[n].width - (m_keys[n].isBlack ? 0 : 2);
                float h = m_pianoHeight * (m_keys[n].isBlack ? 0.63f : 1.0f);
                util::Color c = m_channelColors[info.channel % 16];
                batch.Draw({x, m_pianoY}, {w, h}, {c.r * flash, c.g * flash, c.b * flash, 1.0f});
            }
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
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
    batch.SetBlendMode(true);
    batch.SetTexture(m_gradientTex.Get());
    batch.Draw({0, m_pianoY - 100}, {(float)m_viewW, 100}, {0.1f, 0.15f, 0.3f, 0.1f});
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

void PianoRenderer::DrawSaber(SpriteBatch& batch, const NoteState& state, float) {
    batch.SetBlendMode(true);
    
    // Draw solid cores
    for (int i = 0; i < 128; i++) {
        if (state[i].active) {
            util::Color c = m_channelColors[state[i].channel % 16];
            float x = m_keys[i].x;
            float w = m_keys[i].width;
            batch.Draw({x - w*0.5f, m_pianoY - 2}, {w * 2.0f, 4}, {c.r, c.g, c.b, 1.0f});
            batch.Draw({x - w, m_pianoY - 10}, {w * 3.0f, 20}, {c.r*0.5f, c.g*0.5f, c.b*0.5f, 0.5f});
        }
    }

    // Draw fire/sparks using gradient texture
    batch.SetTexture(m_gradientTex.Get());
    for (int i = 0; i < 128; i++) {
        if (state[i].active) {
            util::Color c = m_channelColors[state[i].channel % 16];
            float x = m_keys[i].x;
            float w = m_keys[i].width;
            batch.Draw({x, m_pianoY - 40}, {w, 40}, {c.r*0.8f, c.g*0.8f, c.b*0.8f, 1.0f});
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

} // namespace pfd
