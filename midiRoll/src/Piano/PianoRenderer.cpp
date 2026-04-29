#include "PianoRenderer.h"
#include <algorithm>
#include <cmath>

namespace pfd {

bool PianoRenderer::Initialize(ID3D11Device* device, uint32_t viewW, uint32_t viewH) {
    m_viewW = viewW;
    m_viewH = viewH;
    m_pianoY = (float)viewH - m_pianoHeight;
    
    if (!particles.Initialize(device)) return false;

    // Initialize channel colors (Vibrant, high-luminance palette for dark backgrounds)
    m_channelColors[0]  = {0.1f, 0.6f, 1.0f, 1.0f}; // Azure Blue
    m_channelColors[1]  = {1.0f, 0.4f, 0.0f, 1.0f}; // Safety Orange
    m_channelColors[2]  = {0.2f, 1.0f, 0.4f, 1.0f}; // Spring Green
    m_channelColors[3]  = {1.0f, 0.2f, 0.6f, 1.0f}; // Deep Pink
    m_channelColors[4]  = {1.0f, 0.9f, 0.0f, 1.0f}; // Electric Yellow
    m_channelColors[5]  = {0.0f, 1.0f, 1.0f, 1.0f}; // Cyan
    m_channelColors[6]  = {1.0f, 0.3f, 0.3f, 1.0f}; // Coral Red
    m_channelColors[7]  = {0.7f, 0.4f, 1.0f, 1.0f}; // Orchid Purple
    m_channelColors[8]  = {0.6f, 1.0f, 0.2f, 1.0f}; // Lime
    m_channelColors[9]  = {0.8f, 0.8f, 0.9f, 1.0f}; // Drums (Bright Silver)
    m_channelColors[10] = {0.2f, 1.0f, 0.7f, 1.0f}; // Mint
    m_channelColors[11] = {1.0f, 0.7f, 0.2f, 1.0f}; // Goldenrod
    m_channelColors[12] = {0.5f, 0.8f, 1.0f, 1.0f}; // Sky Blue
    m_channelColors[13] = {0.9f, 0.6f, 1.0f, 1.0f}; // Lavender
    m_channelColors[14] = {1.0f, 0.6f, 0.6f, 1.0f}; // Salmon
    m_channelColors[15] = {1.0f, 1.0f, 1.0f, 1.0f}; // User Input (White / Neon)

    // Generate remaining colors (16-63) using golden ratio for distinct hues
    for (int i = 16; i < 64; i++) {
        float h = std::fmod((float)i * 0.618033988749895f, 1.0f);
        // Simple HSL to RGB (Saturation 0.8, Lightness 0.6)
        auto hueToRgb = [](float t) {
            t = std::fmod(t, 1.0f);
            if (t < 1.0f/6.0f) return 6.0f * t;
            if (t < 1.0f/2.0f) return 1.0f;
            if (t < 2.0f/3.0f) return (2.0f/3.0f - t) * 6.0f;
            return 0.0f;
        };
        m_channelColors[i] = { hueToRgb(h + 1.0f/3.0f), hueToRgb(h), hueToRgb(h - 1.0f/3.0f), 1.0f };
        // Boost brightness
        m_channelColors[i].r = 0.3f + m_channelColors[i].r * 0.7f;
        m_channelColors[i].g = 0.3f + m_channelColors[i].g * 0.7f;
        m_channelColors[i].b = 0.3f + m_channelColors[i].b * 0.7f;
    }

    CreateTextures(device);
    particles.SetTexture(m_glowTex.Get());

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

    // 3. Radial Glow Texture
    std::vector<uint32_t> glowPixels(64 * 64);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float dx = (x - 31.5f) / 31.5f;
            float dy = (y - 31.5f) / 31.5f;
            float dist = std::sqrt(dx * dx + dy * dy);
            float alpha = std::max(0.0f, 1.0f - dist);
            alpha = std::pow(alpha, 2.0f); // Softer falloff
            uint8_t a = (uint8_t)(alpha * 255.0f);
            glowPixels[y * 64 + x] = (255 << 24) | (a << 16) | (a << 8) | a;
        }
    }
    desc.Width = 64;
    desc.Height = 64;
    initData.pSysMem = glowPixels.data();
    initData.SysMemPitch = 64 * 4;
    ComPtr<ID3D11Texture2D> glowTex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, glowTex.GetAddressOf()))) {
        device->CreateShaderResourceView(glowTex.Get(), nullptr, m_glowTex.GetAddressOf());
    }
}

void PianoRenderer::Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes, float liveTime, float midiPlaybackTime, float dt) {
    (void)dt;

    // 1. Background Atmosphere (Bottom-most layer)
    DrawAtmosphere(batch, state);

    // 2. Floor / Mirror Surface (Optional - can add a separate method or draw here)
    // Darker base for the piano area
    batch.Draw({0, m_pianoY}, {(float)m_viewW, (float)m_viewH - m_pianoY}, {0.03f, 0.03f, 0.05f, 1.0f});

    // 3. Waterfall Notes
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
            util::Color c = m_channelColors[note.channel % 64];
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
        util::Color c = m_channelColors[vn.channel % 64];
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
    // White keys base — darken pressed keys so the color glow reads clearly
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        float x = m_keys[n].x + 1;
        float w = m_keys[n].width - 2;
        bool active = state[n].active;
        // Pressed: draw a dark tinted base so the colour overlay pops (like a black key)
        util::Vec4 base = active ? util::Vec4{0.62f, 0.60f, 0.58f, 1.0f}
                                 : util::Vec4{0.95f, 0.95f, 0.95f, 1.0f};
        batch.Draw({x, m_pianoY}, {w, m_pianoHeight}, base);
        batch.Draw({x, m_pianoY + m_pianoHeight - 4}, {w, 4}, {0, 0, 0, 0.15f});
    }

    // White key highlights (gradient glow — same style as black keys)
    batch.SetBlendMode(true); 
    batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        auto& info = state[n];
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            float x = m_keys[n].x + 1;
            float w = m_keys[n].width - 2;
            util::Color c = m_channelColors[info.channel % 64];
            batch.Draw({x, m_pianoY}, {w, m_pianoHeight}, {c.r, c.g, c.b, 0.4f + flash * 0.6f});
        }
    }

    // 4. Black keys base
    batch.SetBlendMode(false);
    batch.SetTexture(nullptr);
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        float h = m_pianoHeight * 0.63f;
        batch.Draw({x, m_pianoY}, {w, h}, {0.12f, 0.12f, 0.14f, 1.0f});
        batch.Draw({x + 2, m_pianoY}, {w - 4, 3}, {1, 1, 1, 0.1f});
    }

    // 5. Black key highlights
    batch.SetBlendMode(true);
    batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        auto& info = state[n];
        if (info.active) {
            float elapsed = currentTime - (float)info.onTime;
            float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
            float x = m_keys[n].x;
            float w = m_keys[n].width;
            float h = m_pianoHeight * 0.63f;
            util::Color c = m_channelColors[info.channel % 64];
            batch.Draw({x, m_pianoY}, {w, h}, {c.r, c.g, c.b, 0.4f + flash * 0.6f});
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}


float PianoRenderer::GetKeyX(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].x;
}

float PianoRenderer::GetKeyWidth(int note) const {
    if (note < 0 || note >= 128) return 0;
    return m_keys[note].width;
}

void PianoRenderer::DrawImpactFlashes(SpriteBatch& batch, float currentTime) {
    batch.SetBlendMode(true);
    batch.SetTexture(m_glowTex.Get());
    for (auto& flash : m_impacts) {
        float elapsed = currentTime - flash.time;
        float t = elapsed / flash.duration;
        if (t > 1.0f) continue;
        float x = m_keys[flash.note].x + m_keys[flash.note].width * 0.5f;
        float radius = 20.0f + t * 60.0f;
        util::Color c = m_channelColors[0]; // Or use a flash color
        batch.Draw({x - radius, m_pianoY - radius}, {radius * 2, radius * 2}, {1, 0.9f, 0.7f, (1.0f - t) * 0.6f});
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

void PianoRenderer::DrawAtmosphere(SpriteBatch& batch, const NoteState&) {
    // 1. Full-screen background gradient for depth
    batch.SetTexture(m_gradientTex.Get());
    batch.SetBlendMode(false);
    // Dark top to slightly lighter bottom (subtle gradient overlay)
    batch.Draw({0, 0}, {(float)m_viewW, (float)m_viewH}, {0.02f, 0.02f, 0.05f, 0.5f}); 
    
    batch.SetTexture(nullptr);
}

void PianoRenderer::DrawSaber(SpriteBatch& batch, const NoteState& state, float) {
    batch.SetBlendMode(true);
    batch.SetTexture(m_glowTex.Get());
    
    for (int i = 0; i < 128; i++) {
        if (state[i].active) {
            util::Color c = m_channelColors[state[i].channel % 64];
            float x = m_keys[i].x + m_keys[i].width * 0.5f;
            float w = m_keys[i].width;
            
            // Core Glow
            batch.Draw({x - w * 1.5f, m_pianoY - w * 1.5f}, {w * 3.0f, w * 3.0f}, {c.r, c.g, c.b, 0.8f});
            
            // Outer Atmosphere
            float outer = w * 5.0f;
            batch.Draw({x - outer * 0.5f, m_pianoY - outer * 0.5f}, {outer, outer}, {c.r * 0.5f, c.g * 0.5f, c.b * 0.5f, 0.3f});
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);
}

} // namespace pfd
