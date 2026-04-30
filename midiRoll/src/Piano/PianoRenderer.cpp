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
    m_channelColors[0]  = {0.1f, 0.6f, 1.0f, 1.0f}; // Azure Blue
    m_channelColors[1]  = {1.0f, 0.4f, 0.0f, 1.0f}; // Safety Orange
    m_channelColors[2]  = {0.2f, 1.0f, 0.4f, 1.0f}; // Spring Green
    m_channelColors[3]  = {1.0f, 0.2f, 0.6f, 1.0f}; // Deep Pink
    m_channelColors[4]  = {1.0f, 0.9f, 0.0f, 1.0f}; // Electric Yellow
    m_channelColors[5]  = {0.0f, 1.0f, 1.0f, 1.0f}; // Cyan
    m_channelColors[6]  = {1.0f, 0.3f, 0.3f, 1.0f}; // Coral Red
    m_channelColors[7]  = {0.7f, 0.4f, 1.0f, 1.0f}; // Orchid Purple
    m_channelColors[8]  = {0.6f, 1.0f, 0.2f, 1.0f}; // Lime
    m_channelColors[9]  = {0.8f, 0.8f, 0.9f, 1.0f}; // Drums
    m_channelColors[10] = {0.2f, 1.0f, 0.7f, 1.0f}; // Mint
    m_channelColors[11] = {1.0f, 0.7f, 0.2f, 1.0f}; // Goldenrod
    m_channelColors[12] = {0.5f, 0.8f, 1.0f, 1.0f}; // Sky Blue
    m_channelColors[13] = {0.9f, 0.6f, 1.0f, 1.0f}; // Lavender
    m_channelColors[14] = {1.0f, 0.6f, 0.6f, 1.0f}; // Salmon
    m_channelColors[15] = {1.0f, 1.0f, 1.0f, 1.0f}; // User Input

    for (int i = 16; i < 64; i++) {
        float h = std::fmod((float)i * 0.618033988749895f, 1.0f);
        auto hueToRgb = [](float t) {
            t = std::fmod(t, 1.0f);
            if (t < 1.0f/6.0f) return 6.0f * t;
            if (t < 1.0f/2.0f) return 1.0f;
            if (t < 2.0f/3.0f) return (2.0f/3.0f - t) * 6.0f;
            return 0.0f;
        };
        m_channelColors[i] = { hueToRgb(h + 1.0f/3.0f), hueToRgb(h), hueToRgb(h - 1.0f/3.0f), 1.0f };
        m_channelColors[i].r = 0.3f + m_channelColors[i].r * 0.7f;
        m_channelColors[i].g = 0.3f + m_channelColors[i].g * 0.7f;
        m_channelColors[i].b = 0.3f + m_channelColors[i].b * 0.7f;
    }

    CreateTextures(device);
    particles.SetTexture(m_glowTex.Get());
    gpuNotes.Initialize(device);

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
    m_pianoHeight = keyW * 4.5f;
    if (m_pianoHeight > m_viewH * 0.3f) m_pianoHeight = m_viewH * 0.3f;
    m_pianoY = (float)m_viewH - m_pianoHeight;
    float x = 0;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        int m = n % 12;
        bool isBlack = (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
        m_keys[n].isBlack = isBlack;
        m_keys[n].width = isBlack ? keyW * 0.65f : keyW;
        if (isBlack) m_keys[n].x = x - m_keys[n].width * 0.5f;
        else { m_keys[n].x = x; x += keyW; }
    }
}

void PianoRenderer::UpdateGpuNotes(ID3D11Device* device, const std::vector<Note>& notes) {
    gpuNotes.UpdateMidiNotes(device, notes);
}

void PianoRenderer::Update(NoteState& state, float currentTime, float dt) {
    (void)dt;
    state.UpdateVisualNotes(currentTime);
    for (int note : state.RecentNoteOns()) {
        float x = m_keys[note].x + m_keys[note].width * 0.5f;
        util::Color c = GetChannelColor(state[note].channel);
        if (m_keys[note].isBlack) { c.r *= 0.8f; c.g *= 0.8f; c.b *= 0.8f; }
        particles.EmitSparks(x, m_pianoY, c);
        m_impacts.push_back({ note, currentTime, 0.25f });
    }
    state.ClearRecentEvents();
    m_impacts.erase(std::remove_if(m_impacts.begin(), m_impacts.end(), [&](const ImpactFlash& f) { return currentTime - f.time > f.duration; }), m_impacts.end());
}

void PianoRenderer::CreateTextures(ID3D11Device* device) {
    const int TEX_SIZE = 64;
    std::vector<uint32_t> pixels(TEX_SIZE * TEX_SIZE);
    float radius = 12.0f; float border = 3.0f; float center = TEX_SIZE / 2.0f;
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            float dx = std::max(0.0f, std::abs(x - center) - (center - radius));
            float dy = std::max(0.0f, std::abs(y - center) - (center - radius));
            float dist = std::sqrt(dx*dx + dy*dy);
            float alpha = dist > radius ? 0.0f : (dist > radius - 1.0f ? radius - dist : 1.0f);
            float intensity = dist >= radius - border ? 1.0f : 0.5f;
            uint8_t a = (uint8_t)(intensity * alpha * 255.0f);
            pixels[y * TEX_SIZE + x] = (255 << 24) | (a << 16) | (a << 8) | a;
        }
    }
    D3D11_TEXTURE2D_DESC desc{TEX_SIZE, TEX_SIZE, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1,0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE};
    D3D11_SUBRESOURCE_DATA initData{pixels.data(), TEX_SIZE * 4};
    ComPtr<ID3D11Texture2D> tex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, tex.GetAddressOf()))) device->CreateShaderResourceView(tex.Get(), nullptr, m_noteTex.GetAddressOf());

    std::vector<uint32_t> gradPixels(64);
    for (int y = 0; y < 64; y++) gradPixels[y] = (255 << 24) | ((uint8_t)((1.0f - (float)y / 63.0f) * 255.0f) << 16); // Blue/Green etc handled by tint
    desc.Width = 1; desc.Height = 64; initData.pSysMem = gradPixels.data(); initData.SysMemPitch = 4;
    ComPtr<ID3D11Texture2D> gradTex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, gradTex.GetAddressOf()))) device->CreateShaderResourceView(gradTex.Get(), nullptr, m_gradientTex.GetAddressOf());

    std::vector<uint32_t> glowPixels(64 * 64);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float dx = (x - 31.5f) / 31.5f, dy = (y - 31.5f) / 31.5f;
            float alpha = std::pow(std::max(0.0f, 1.0f - std::sqrt(dx*dx + dy*dy)), 2.0f);
            uint8_t a = (uint8_t)(alpha * 255.0f);
            glowPixels[y * 64 + x] = (255 << 24) | (a << 16) | (a << 8) | a;
        }
    }
    desc.Width = 64; desc.Height = 64; initData.pSysMem = glowPixels.data(); initData.SysMemPitch = 64 * 4;
    ComPtr<ID3D11Texture2D> glowTex;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &initData, glowTex.GetAddressOf()))) device->CreateShaderResourceView(glowTex.Get(), nullptr, m_glowTex.GetAddressOf());
}

void PianoRenderer::Render(SpriteBatch& batch, const NoteState& state, const std::vector<Note>& midiNotes, float liveTime, float midiPlaybackTime, float dt, ID3D11DeviceContext* d3dCtx) {
    (void)midiNotes; (void)midiPlaybackTime;
    ComPtr<ID3D11Device> device; d3dCtx->GetDevice(device.GetAddressOf());
    particles.Update(device.Get(), d3dCtx, dt);
    DrawAtmosphere(batch, state);
    batch.Draw({0, m_pianoY}, {(float)m_viewW, (float)m_viewH - m_pianoY}, {0.03f, 0.03f, 0.05f, 1.0f});
    std::array<GPUNoteSystem::KeyLayout, 128> layout;
    for (int i = 0; i < 128; i++) {
        layout[i].x = m_keys[i].x;
        layout[i].width = m_keys[i].width;
        layout[i].isBlack = m_keys[i].isBlack ? 1.0f : 0.0f;
        layout[i].pad = 0;
    }
    gpuNotes.Render(d3dCtx, state.GetVisualNotes(), m_channelColors, layout, liveTime, m_noteSpeed, m_pianoY, (float)m_viewW, (float)m_viewH, m_falling);
    for (const auto& vn : state.GetVisualNotes()) {
        if (vn.active) {
            float noteX = m_keys[vn.note].x + 1, noteW = m_keys[vn.note].width - 2;
            particles.EmitContinuous(noteX + noteW * 0.5f, m_pianoY, noteW, GetChannelColor(vn.channel));
        }
    }
    particles.Draw(d3dCtx, batch);
    batch.SetTexture(nullptr); batch.SetBlendMode(false); 
    DrawPiano(batch, state, liveTime);
    DrawSaber(batch, state, liveTime);
}

void PianoRenderer::DrawPiano(SpriteBatch& batch, const NoteState& state, float currentTime) {
    float frontH = 8.0f; batch.Draw({0, m_pianoY}, {(float)m_viewW, m_pianoHeight + frontH}, {0.03f, 0.03f, 0.05f, 1.0f});
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack) continue;
        float x = m_keys[n].x + 1, w = m_keys[n].width - 2;
        bool active = state[n].active; float pressOff = active ? 2.0f : 0.0f;
        util::Vec4 topC = active ? util::Vec4{0.5f, 0.48f, 0.46f, 1.0f} : util::Vec4{0.92f, 0.92f, 0.92f, 1.0f};
        util::Vec4 botC = active ? util::Vec4{0.42f, 0.40f, 0.38f, 1.0f} : util::Vec4{0.82f, 0.82f, 0.82f, 1.0f};
        float halfH = (m_pianoHeight - frontH) * 0.5f;
        batch.Draw({x, m_pianoY + pressOff}, {w, halfH}, topC);
        batch.Draw({x, m_pianoY + pressOff + halfH}, {w, halfH}, botC);
        batch.Draw({x, m_pianoY + pressOff}, {2.0f, m_pianoHeight - frontH}, {1, 1, 1, active ? 0.05f : 0.15f});
        batch.Draw({x + w - 2.0f, m_pianoY + pressOff}, {2.0f, m_pianoHeight - frontH}, {0, 0, 0, active ? 0.1f : 0.12f});
        batch.Draw({x, m_pianoY + m_pianoHeight - frontH + pressOff}, {w, frontH}, active ? util::Vec4{0.3f, 0.28f, 0.26f, 1.0f} : util::Vec4{0.68f, 0.68f, 0.68f, 1.0f});
    }
    batch.SetBlendMode(true); batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (m_keys[n].isBlack || state[n].activeChannels == 0) continue;
        float flash = std::max(0.0f, 1.0f - (currentTime - (float)state[n].onTime) * 4.0f);
        util::Color c = GetChannelColor(state[n].channel);
        batch.Draw({m_keys[n].x + 1, m_pianoY + 2.0f}, {m_keys[n].width - 2, m_pianoHeight - frontH}, {c.r, c.g, c.b, 0.45f + flash * 0.55f});
    }
    batch.SetTexture(nullptr); batch.SetBlendMode(false);
    float blackH = (m_pianoHeight - frontH) * 0.63f, blackFrontH = 5.0f;
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack) continue;
        float x = m_keys[n].x, w = m_keys[n].width; bool active = state[n].active; float pressOff = active ? 1.5f : 0.0f;
        batch.Draw({x, m_pianoY + pressOff}, {w, blackH}, active ? util::Vec4{0.08f, 0.08f, 0.1f, 1.0f} : util::Vec4{0.12f, 0.12f, 0.14f, 1.0f});
        batch.Draw({x, m_pianoY + blackH + pressOff}, {w, blackFrontH}, {0.06f, 0.06f, 0.07f, 1.0f});
    }
    batch.SetBlendMode(true); batch.SetTexture(m_gradientTex.Get());
    for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
        if (!m_keys[n].isBlack || state[n].activeChannels == 0) continue;
        float flash = std::max(0.0f, 1.0f - (currentTime - (float)state[n].onTime) * 4.0f);
        util::Color c = GetChannelColor(state[n].channel);
        batch.Draw({m_keys[n].x, m_pianoY + 1.5f}, {m_keys[n].width, blackH}, {c.r, c.g, c.b, 0.45f + flash * 0.55f});
    }
    batch.SetTexture(nullptr); batch.SetBlendMode(false);
}

float PianoRenderer::GetKeyX(int note) const { return (note < 0 || note >= 128) ? 0 : m_keys[note].x; }
float PianoRenderer::GetKeyWidth(int note) const { return (note < 0 || note >= 128) ? 0 : m_keys[note].width; }

void PianoRenderer::DrawImpactFlashes(SpriteBatch& batch, float currentTime) {
    batch.SetBlendMode(true); batch.SetTexture(m_glowTex.Get());
    for (auto& flash : m_impacts) {
        float t = (currentTime - flash.time) / flash.duration; if (t > 1.0f) continue;
        float x = m_keys[flash.note].x + m_keys[flash.note].width * 0.5f, radius = 20.0f + t * 60.0f;
        batch.Draw({x - radius, m_pianoY - radius}, {radius * 2, radius * 2}, {1, 0.9f, 0.7f, (1.0f - t) * 0.6f});
    }
    batch.SetTexture(nullptr); batch.SetBlendMode(false);
}

void PianoRenderer::DrawAtmosphere(SpriteBatch& batch, const NoteState&) {
    batch.SetTexture(m_gradientTex.Get()); batch.SetBlendMode(false);
    batch.Draw({0, 0}, {(float)m_viewW, (float)m_viewH}, {0.02f, 0.02f, 0.05f, 0.5f});
    batch.SetTexture(nullptr);
}

void PianoRenderer::DrawSaber(SpriteBatch& batch, const NoteState& state, float) {
    batch.SetBlendMode(true); batch.SetTexture(m_glowTex.Get());
    util::Color sc = GetChannelColor(m_saberColorIdx);
    batch.Draw({0, m_pianoY - 2}, {(float)m_viewW, 4}, {sc.r, sc.g, sc.b, 0.95f});
    batch.Draw({0, m_pianoY - 12}, {(float)m_viewW, 24}, {sc.r, sc.g, sc.b, 0.5f});
    for (int i = 0; i < 128; i++) {
        if (state[i].active) {
            util::Color c = GetChannelColor(state[i].channel);
            float x = m_keys[i].x + m_keys[i].width * 0.5f, w = m_keys[i].width;
            batch.Draw({x - w * 1.5f, m_pianoY - w * 1.5f}, {w * 3.0f, w * 3.0f}, {c.r, c.g, c.b, 0.8f});
        }
    }
    batch.SetTexture(nullptr); batch.SetBlendMode(false);
}

} // namespace pfd
