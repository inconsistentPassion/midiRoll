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

    // 4. Caustics/Noise Texture
    std::vector<uint32_t> noisePixels(128 * 128);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            float fx = (float)x / 128.0f * 6.28318f;
            float fy = (float)y / 128.0f * 6.28318f;
            float v = std::sinf(fx * 2.0f + std::cosf(fy * 3.0f)) + 
                      std::cosf(fy * 2.0f + std::sinf(fx * 3.0f));
            v = (v + 2.0f) * 0.25f; // 0-1
            v = std::pow(v, 2.5f); // sharpen for caustics look
            uint8_t a = (uint8_t)(std::min(1.0f, v * 1.5f) * 255.0f);
            noisePixels[y * 128 + x] = (255 << 24) | (a << 16) | (a << 8) | a;
        }
    }
    desc.Width = 128;
    desc.Height = 128;
    initData.pSysMem = noisePixels.data();
    initData.SysMemPitch = 128 * 4;
    ComPtr<ID3D11Texture2D> noiseTex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, noiseTex.GetAddressOf()))) {
        device->CreateShaderResourceView(noiseTex.Get(), nullptr, m_noiseTex.GetAddressOf());
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

    // Helper: compute note rect for bloom/caustics (returns false if offscreen)
    auto computeMidiRect = [&](const Note& note, float& outY, float& outH) -> bool {
        float yS, yE;
        if (m_falling) {
            if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) return false;
            yE = m_pianoY - (float)(note.start - midiPlaybackTime) * m_noteSpeed;
            yS = m_pianoY - (float)(note.end - midiPlaybackTime) * m_noteSpeed;
            if (yE > m_pianoY) yE = m_pianoY;
            if (yS < 0) yS = 0;
        } else {
            if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) return false;
            yE = m_pianoY + (float)(note.start - midiPlaybackTime) * m_noteSpeed;
            yS = m_pianoY + (float)(note.end - midiPlaybackTime) * m_noteSpeed;
            float pianoBottom = m_pianoY + m_pianoHeight;
            if (yE < pianoBottom) yE = pianoBottom;
            if (yS > (float)m_viewH) yS = (float)m_viewH;
        }
        outH = std::abs(yE - yS);
        outY = std::min(yS, yE);
        return outH > 1.0f;
    };

    // Helper to get note color with darkening for black keys
    auto getNoteColor = [&](int noteNumber, int channel) {
        util::Color c = m_channelColors[channel % 64];
        if (m_keys[noteNumber].isBlack) {
            c.r *= 0.8f;
            c.g *= 0.8f;
            c.b *= 0.8f;
        }
        return c;
    };
    
    // ── PARTICLES PASS (drawn behind everything else) ──
    particles.Draw(batch);

    // ── BLOOM PASS (drawn first, behind the solid note bars) ──
    // Uses the radial glow texture for soft falloff, same as particles
    batch.SetTexture(m_glowTex.Get());
    batch.SetBlendMode(true);
    float bloomSpread = 5.0f;
    float pianoBodyTop    = m_pianoY;
    float pianoBodyBottom = m_pianoY + m_pianoHeight;

    auto drawBloom = [&](float noteX, float noteW, float ny, float nh, util::Color c) {
        // Expand bloom outward
        float bx = noteX - bloomSpread;
        float bw = noteW + bloomSpread * 2;
        float by = ny - bloomSpread;
        float bh = nh + bloomSpread * 2;
        // Clamp: bloom must not enter the piano body
        if (m_falling) {
            // Notes are above piano — clamp bottom of bloom at pianoBodyTop
            float maxBy = pianoBodyTop;
            if (by + bh > maxBy) bh = maxBy - by;
        } else {
            // Notes are below piano — clamp top of bloom at pianoBodyBottom
            if (by < pianoBodyBottom) { bh -= (pianoBodyBottom - by); by = pianoBodyBottom; }
        }
        if (bh <= 0) return;
        batch.Draw({bx, by}, {bw, bh}, {c.r, c.g, c.b, 0.15f});
    };

    // Bloom for MIDI notes (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& note : midiNotes) {
            if (m_keys[note.note].isBlack != blackPass) continue;
            float noteX = m_keys[note.note].x + 1;
            float noteW = m_keys[note.note].width - 2;
            float ny, nh;
            if (!computeMidiRect(note, ny, nh)) continue;
            drawBloom(noteX, noteW, ny, nh, getNoteColor(note.note, note.channel));
        }
    }

    // Bloom for LIVE notes (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& vn : state.GetVisualNotes()) {
            if (m_keys[vn.note].isBlack != blackPass) continue;
            float noteX = m_keys[vn.note].x + 1;
            float noteW = m_keys[vn.note].width - 2;
            float yT, yB;
            if (m_falling) {
                if (vn.active) { yT = m_pianoY; yB = m_pianoY + (liveTime - (float)vn.onTime) * m_noteSpeed; }
                else { float d=(float)vn.offTime-(float)vn.onTime, r=liveTime-(float)vn.offTime; yT=m_pianoY+r*m_noteSpeed; yB=m_pianoY+(d+r)*m_noteSpeed; if(yT>(float)m_viewH) continue; }
                float pb = m_pianoY + m_pianoHeight; if (yT < pb) yT = pb; if (yT >= yB) continue;
            } else {
                if (vn.active) { yB = m_pianoY; yT = m_pianoY - (liveTime - (float)vn.onTime) * m_noteSpeed; }
                else { float d=(float)vn.offTime-(float)vn.onTime, r=liveTime-(float)vn.offTime; yB=m_pianoY-r*m_noteSpeed; yT=m_pianoY-(d+r)*m_noteSpeed; if(yB<0) continue; }
                if (yB > m_pianoY) yB = m_pianoY; if (yT >= yB) continue;
            }
            float ny = std::min(yT, yB), nh = std::abs(yB - yT);
            util::Color c = getNoteColor(vn.note, vn.channel);
            // Reduced bloom for released notes
            if (!vn.active) { c.a *= 0.4f; } 
            drawBloom(noteX, noteW, ny, nh, c);
        }
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);

    // ── SOLID NOTE BARS ──
    batch.SetTexture(m_noteTex.Get());
    // 1. Draw MIDI notes (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& note : midiNotes) {
            if (m_keys[note.note].isBlack != blackPass) continue;
            float noteX = m_keys[note.note].x + 1;
            float noteW = m_keys[note.note].width - 2;
            float yS, yE;

            if (m_falling) {
                if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
                yE = m_pianoY - (float)(note.start - midiPlaybackTime) * m_noteSpeed;
                yS = m_pianoY - (float)(note.end - midiPlaybackTime) * m_noteSpeed;
                if (yE > m_pianoY) yE = m_pianoY; // clip at piano top
                if (yS < 0) yS = 0;
            } else {
                if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
                yE = m_pianoY + (float)(note.start - midiPlaybackTime) * m_noteSpeed;
                yS = m_pianoY + (float)(note.end - midiPlaybackTime) * m_noteSpeed;
                // Clip: don't draw inside the piano body
                float pianoBottom = m_pianoY + m_pianoHeight;
                if (yE < pianoBottom) yE = pianoBottom;
                if (yS > (float)m_viewH) yS = (float)m_viewH;
            }

            if (std::abs(yE - yS) > 1.0f) {
                drawRoundedNote(noteX, std::min(yS, yE), noteW, std::abs(yE - yS), getNoteColor(note.note, note.channel));
            }
        }
    }

    // 2. Draw LIVE notes (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& vn : state.GetVisualNotes()) {
            if (m_keys[vn.note].isBlack != blackPass) continue;
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
                // Clip: don't draw inside the piano body
                float pianoBottom = m_pianoY + m_pianoHeight;
                if (yT < pianoBottom) yT = pianoBottom;
                if (yT >= yB) continue;
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
                // Clip: don't draw inside the piano body
                if (yB > m_pianoY) yB = m_pianoY;
                if (yT >= yB) continue;
            }
            util::Color c = getNoteColor(vn.note, vn.channel);
            drawRoundedNote(noteX, std::min(yT, yB), noteW, std::abs(yB - yT), c);
        }
    }

    // 3. Emit particle trails for active notes
    for (const auto& vn : state.GetVisualNotes()) {
        if (vn.active) {
            float noteX = m_keys[vn.note].x + 1;
            float noteW = m_keys[vn.note].width - 2;
            particles.EmitContinuous(noteX + noteW * 0.5f, m_pianoY, noteW, getNoteColor(vn.note, vn.channel));
        }
    }

    // --- CAUSTICS OVERLAY PASS ---
    // Drawn on top of the note bars using the same coordinates.
    // Uses the noise texture with two scrolling layers for a water-caustics look.
    batch.SetBlendMode(true);
    batch.SetTexture(m_noiseTex.Get());
    auto drawCaustics = [&](float x, float y, float w, float h, util::Color c, float yBase) {
        if (h <= 4.0f) return;
        
        // Use a smaller inset (2px) for the main pattern to cover more area
        float inset = 2.0f;
        float ix = x + 1.0f;
        float iw = w - 2.0f;
        float iy = y + inset;
        float ih = h - inset * 2.0f;
        
        // Layer 1 & 2: Main pattern (inset to respect rounded corners mostly)
        if (ih > 0) {
            float uvScale = 0.005f;
            float uvScroll = liveTime * 1.5f;
            
            // Calculate UVs relative to yBase so the pattern 'sticks' to the note
            float uvY1 = (iy - yBase) * uvScale - uvScroll;
            float uvY2 = (iy + ih - yBase) * uvScale - uvScroll;
            float uvX1 = ix * uvScale;
            float uvX2 = (ix + iw) * uvScale;

            batch.Draw({ix, iy}, {iw, ih}, m_noiseTex.Get(), {uvX1, uvY1}, {uvX2, uvY2}, {c.r, c.g, c.b, 0.25f});
            
            // Interference layer
            float uvScale2 = 0.008f;
            float uvScroll2 = liveTime * 0.8f;
            float uvY2_1 = (iy - yBase) * uvScale2 - uvScroll2;
            float uvY2_2 = (iy + ih - yBase) * uvScale2 - uvScroll2;
            float uvX2_1 = ix * uvScale2 + uvScroll2 * 0.4f;
            float uvX2_2 = ((ix + iw) * uvScale2) + uvScroll2 * 0.4f;
            batch.Draw({ix, iy}, {iw, ih}, m_noiseTex.Get(), {uvX2_1, uvY2_1}, {uvX2_2, uvY2_2}, {c.r, c.g, c.b, 0.25f});
        }

        // Layer 3: Subtle full-note bleed (0px inset)
        // This fills the edges/corners with a faint shimmer so they don't look 'empty'
        batch.Draw({x + 1, y}, {w - 2, h}, m_noiseTex.Get(), 
                   {x * 0.004f, (y - yBase) * 0.004f}, {(x + w) * 0.004f, (y + h - yBase) * 0.004f}, 
                   {c.r, c.g, c.b, 0.12f});
    };

    // 1. MIDI Notes Caustics (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& note : midiNotes) {
            if (m_keys[note.note].isBlack != blackPass) continue;
            float noteX = m_keys[note.note].x + 1;
            float noteW = m_keys[note.note].width - 2;
            float yS, yE, yBase;
            if (m_falling) {
                if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
                yE = m_pianoY - (float)(note.start - midiPlaybackTime) * m_noteSpeed;
                yS = m_pianoY - (float)(note.end - midiPlaybackTime) * m_noteSpeed;
                yBase = yS; // theoretical top of note
                if (yE > m_pianoY) yE = m_pianoY;
                if (yS < 0) yS = 0;
            } else {
                if (note.end < midiPlaybackTime || note.start > midiPlaybackTime + 5.0) continue;
                yE = m_pianoY + (float)(note.start - midiPlaybackTime) * m_noteSpeed;
                yS = m_pianoY + (float)(note.end - midiPlaybackTime) * m_noteSpeed;
                yBase = yE; // theoretical top of note (in rising, yE is lower value)
                float pianoBottom = m_pianoY + m_pianoHeight;
                if (yE < pianoBottom) yE = pianoBottom;
                if (yS > (float)m_viewH) yS = (float)m_viewH;
            }
            if (std::abs(yE - yS) > 1.0f) {
                drawCaustics(noteX, std::min(yS, yE), noteW, std::abs(yE - yS), getNoteColor(note.note, note.channel), yBase);
            }
        }
    }

    // 2. LIVE Notes Caustics (White then Black)
    for (int pass = 0; pass < 2; pass++) {
        bool blackPass = (pass == 1);
        for (const auto& vn : state.GetVisualNotes()) {
            if (m_keys[vn.note].isBlack != blackPass) continue;
            float noteX = m_keys[vn.note].x + 1;
            float noteW = m_keys[vn.note].width - 2;
            float yT, yB, yBase;
            if (m_falling) {
                if (vn.active) {
                    yT = m_pianoY; 
                    yB = m_pianoY + (liveTime - (float)vn.onTime) * m_noteSpeed;
                    yBase = m_pianoY; // simplify for active
                } else {
                    float d = (float)vn.offTime - (float)vn.onTime;
                    float r = liveTime - (float)vn.offTime;
                    yT = m_pianoY + r * m_noteSpeed;
                    yB = m_pianoY + (d + r) * m_noteSpeed;
                    yBase = yT;
                    if (yT > (float)m_viewH) continue;
                }
                float pb = m_pianoY + m_pianoHeight;
                if (yT < pb) yT = pb;
                if (yT >= yB) continue;
            } else {
                if (vn.active) {
                    yB = m_pianoY;
                    yT = m_pianoY - (liveTime - (float)vn.onTime) * m_noteSpeed;
                    yBase = yT;
                } else {
                    float d = (float)vn.offTime - (float)vn.onTime;
                    float r = liveTime - (float)vn.offTime;
                    yB = m_pianoY - r * m_noteSpeed;
                    yT = m_pianoY - (d + r) * m_noteSpeed;
                    yBase = yT;
                    if (yB < 0) continue;
                }
                if (yB > m_pianoY) yB = m_pianoY;
                if (yT >= yB) continue;
            }
            drawCaustics(noteX, std::min(yT, yB), noteW, std::abs(yB - yT), getNoteColor(vn.note, vn.channel), yBase);
        }
    }

    // Restore default state for piano rendering
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false); // CRITICAL: Stop additive blending before drawing the piano

    DrawPiano(batch, state, liveTime);
    DrawSaber(batch, state, liveTime);
}

void PianoRenderer::DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime) {
    float frontH = 8.0f;   // 3D front face depth
    float bevelW = 2.0f;   // side bevel thickness

    // Dark recessed bed behind all keys
    batch.Draw({0, m_pianoY}, {(float)m_viewW, m_pianoHeight + frontH},
               {0.03f, 0.03f, 0.05f, 1.0f});

    // ── WHITE KEYS ──
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        float x = m_keys[n].x + 1;
        float w = m_keys[n].width - 2;
        bool active = state[n].active;
        float pressOff = active ? 2.0f : 0.0f;

        // Key top surface (vertical gradient: light top, darker bottom)
        util::Vec4 topC = active ? util::Vec4{0.50f, 0.48f, 0.46f, 1.0f}
                                 : util::Vec4{0.92f, 0.92f, 0.92f, 1.0f};
        util::Vec4 botC = active ? util::Vec4{0.42f, 0.40f, 0.38f, 1.0f}
                                 : util::Vec4{0.82f, 0.82f, 0.82f, 1.0f};
        float halfH = (m_pianoHeight - frontH) * 0.5f;
        batch.Draw({x, m_pianoY + pressOff}, {w, halfH}, topC);
        batch.Draw({x, m_pianoY + pressOff + halfH}, {w, halfH}, botC);

        // Left bevel highlight
        batch.Draw({x, m_pianoY + pressOff}, {bevelW, m_pianoHeight - frontH},
                   {1.0f, 1.0f, 1.0f, active ? 0.05f : 0.15f});
        // Right bevel shadow
        batch.Draw({x + w - bevelW, m_pianoY + pressOff}, {bevelW, m_pianoHeight - frontH},
                   {0.0f, 0.0f, 0.0f, active ? 0.1f : 0.12f});

        // Front face (3D depth)
        float frontY = m_pianoY + m_pianoHeight - frontH + pressOff;
        util::Vec4 fCol = active ? util::Vec4{0.30f, 0.28f, 0.26f, 1.0f}
                                 : util::Vec4{0.68f, 0.68f, 0.68f, 1.0f};
        batch.Draw({x, frontY}, {w, frontH}, fCol);

        // Top specular line
        if (!active)
            batch.Draw({x + 2, m_pianoY + 1}, {w - 4, 1.0f}, {1, 1, 1, 0.25f});
        // Bottom shadow
        batch.Draw({x, frontY + frontH - 1}, {w, 1.0f}, {0, 0, 0, 0.3f});
    }

    // ── White key color glow ──
    batch.SetBlendMode(true);
    batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        auto& info = state[n];
        if (info.activeChannels == 0) continue;
        float elapsed = currentTime - (float)info.onTime;
        float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
        float x = m_keys[n].x + 1;
        float w = m_keys[n].width - 2;
        util::Color c = m_channelColors[info.channel % 64];
        batch.Draw({x, m_pianoY + 2.0f}, {w, m_pianoHeight - frontH},
                   {c.r, c.g, c.b, 0.45f + flash * 0.55f});
        batch.Draw({x, m_pianoY + m_pianoHeight - frontH + 2.0f}, {w, frontH},
                   {c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 0.3f + flash * 0.3f});
    }
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);

    // ── BLACK KEYS ──
    float blackH = (m_pianoHeight - frontH) * 0.63f;
    float blackFrontH = 5.0f;

    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        bool active = state[n].active;
        float pressOff = active ? 1.5f : 0.0f;

        // Key body
        util::Vec4 body = active ? util::Vec4{0.08f, 0.08f, 0.10f, 1.0f}
                                 : util::Vec4{0.12f, 0.12f, 0.14f, 1.0f};
        batch.Draw({x, m_pianoY + pressOff}, {w, blackH}, body);

        // Top specular highlight + center gloss
        if (!active) {
            batch.Draw({x + 2, m_pianoY + 1}, {w - 4, 2.0f}, {1, 1, 1, 0.08f});
            batch.Draw({x + 3, m_pianoY + 3}, {w - 6, blackH * 0.3f}, {1, 1, 1, 0.04f});
        }
        // Left bevel highlight
        batch.Draw({x, m_pianoY + pressOff}, {1.5f, blackH}, {1, 1, 1, 0.06f});
        // Right bevel shadow
        batch.Draw({x + w - 1.5f, m_pianoY + pressOff}, {1.5f, blackH}, {0, 0, 0, 0.2f});

        // Front face
        float bFY = m_pianoY + blackH + pressOff;
        batch.Draw({x, bFY}, {w, blackFrontH}, {0.06f, 0.06f, 0.07f, 1.0f});
        batch.Draw({x, bFY + blackFrontH - 1}, {w, 1.5f}, {0, 0, 0, 0.4f});

        // Drop shadow cast onto white keys
        batch.SetBlendMode(true);
        batch.Draw({x - 1, bFY + blackFrontH}, {w + 2, 4.0f}, {0, 0, 0, 0.15f});
        batch.SetBlendMode(false);
    }

    // ── Black key color glow ──
    batch.SetBlendMode(true);
    batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        auto& info = state[n];
        if (info.activeChannels == 0) continue;
        float elapsed = currentTime - (float)info.onTime;
        float flash = std::max(0.0f, 1.0f - elapsed * 4.0f);
        float x = m_keys[n].x;
        float w = m_keys[n].width;
        util::Color c = m_channelColors[info.channel % 64];
        batch.Draw({x, m_pianoY + 1.5f}, {w, blackH},
                   {c.r, c.g, c.b, 0.45f + flash * 0.55f});
        batch.Draw({x, m_pianoY + blackH + 1.5f}, {w, blackFrontH},
                   {c.r * 0.5f, c.g * 0.5f, c.b * 0.5f, 0.25f + flash * 0.25f});
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
    
    // Global Saber Line at the top of piano
    util::Color sc = m_channelColors[m_saberColorIdx % 64];
    batch.Draw({0, m_pianoY - 2}, {(float)m_viewW, 4}, {sc.r, sc.g, sc.b, 0.95f});
    batch.Draw({0, m_pianoY - 12}, {(float)m_viewW, 24}, {sc.r, sc.g, sc.b, 0.5f});
    batch.Draw({0, m_pianoY - 35}, {(float)m_viewW, 70}, {sc.r, sc.g, sc.b, 0.2f});
    
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
