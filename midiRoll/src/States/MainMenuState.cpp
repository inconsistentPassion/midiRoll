#include "MainMenuState.h"
#include "StateHelpers.h"
#include "../Input/MidiInput.h"
#include <algorithm>
#include <cmath>
#include <Windows.h>
#include <commdlg.h>

namespace pfd {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string MainMenuState::BuildMidiLabel(int deviceIndex) const {
    int count = MidiInput::DeviceCount();
    if (count == 0 || deviceIndex < 0) return "MIDI: none";

    MIDIINCAPSW caps{};
    if (midiInGetDevCapsW(static_cast<UINT>(deviceIndex), &caps, sizeof(caps)) != MMSYSERR_NOERROR)
        return "MIDI: device " + std::to_string(deviceIndex + 1);

    int len = WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, nullptr, 0, nullptr, nullptr);
    std::string name;
    if (len > 0) { name.resize(len - 1); WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name.data(), len, nullptr, nullptr); }

    return "MIDI: " + name + " [" + std::to_string(deviceIndex + 1) + "/" + std::to_string(count) + "]";
}

void MainMenuState::CycleMidiDevice(Context& ctx) {
    int count = MidiInput::DeviceCount();
    if (count == 0) {
        m_midiDeviceIndex = -1;
        ctx.midiInput->Close();
        m_items[3].label = "MIDI: none";
        return;
    }
    
    // Cycle: -1 -> 0 -> 1 -> ... -> count-1 -> -1
    m_midiDeviceIndex = (m_midiDeviceIndex + 2) % (count + 1) - 1;
    
    if (m_midiDeviceIndex < 0) {
        ctx.midiInput->Close();
        m_items[3].label = "MIDI: none";
    } else {
        bool ok = ctx.midiInput->Open(m_midiDeviceIndex);
        if (ok) {
            m_items[3].label = BuildMidiLabel(m_midiDeviceIndex);
        } else {
            // Device listed but couldn't open — skip to next
            // Recursively try the next device
            CycleMidiDevice(ctx);
        }
    }
}

// ---------------------------------------------------------------------------
// State lifecycle
// ---------------------------------------------------------------------------

void MainMenuState::Enter(Context& ctx) {
    m_selected = 0;
    m_hovered = -1;
    m_bgNotes.clear();
    m_spawnTimer = 0;
    m_titleAnim = 0;
    m_enterAnim = 1.0f;
    ctx.audio->AllNotesOff();
    ctx.input->ClearEvents();

    // Sync MIDI button label with whatever device is currently open
    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        m_midiDeviceIndex = ctx.midiInput->DeviceIndex();
        m_items[3].label = BuildMidiLabel(m_midiDeviceIndex);
    } else {
        m_midiDeviceIndex = -1;
        m_items[3].label = "MIDI: none";
    }
}

void MainMenuState::Exit(Context& ctx) {
    (void)ctx;
}

Transition MainMenuState::Update(Context& ctx, double dt) {
    m_titleAnim += (float)dt;
    m_enterAnim = std::max(0.0f, m_enterAnim - (float)dt * 2.0f);

    // Smooth hover animations
    for (int i = 0; i < (int)m_items.size(); i++) {
        float target = (i == m_hovered) ? 1.0f : 0.0f;
        m_items[i].hoverAnim += (target - m_items[i].hoverAnim) * std::min(1.0f, (float)dt * 12.0f);
    }

    // Spawn background notes
    m_spawnTimer += (float)dt;
    if (m_spawnTimer > 0.15f) {
        m_spawnTimer = 0;
        SpawnBackgroundNote(ctx);
    }

    // Update background notes
    float viewH = (float)ctx.window->Height();
    for (auto& n : m_bgNotes) {
        n.y += n.speed * (float)dt;
        n.alpha -= 0.15f * (float)dt;
    }
    m_bgNotes.erase(
        std::remove_if(m_bgNotes.begin(), m_bgNotes.end(),
            [&](const FallingNote& n) { return n.y > viewH || n.alpha <= 0; }),
        m_bgNotes.end());

    return {};
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void MainMenuState::Render(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    batch.Begin(ctx.d3d->Context(), vw, vh);

    DrawBackground(ctx);
    DrawTitle(ctx);
    DrawButtons(ctx);
    DrawHint(ctx);

    // Fade-in overlay
    if (m_enterAnim > 0) {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, m_enterAnim});
    }

    batch.End();
}

void MainMenuState::DrawBackground(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Dark gradient background
    batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, 1.0f});

    // Subtle radial glow in center
    batch.SetBlendMode(true);
    batch.SetTexture(ctx.piano->GetGradientTex());
    float cx = vw * 0.5f, cy = vh * 0.4f;
    float glowR = 400.0f + std::sinf(m_titleAnim * 0.5f) * 30.0f;
    batch.Draw({cx - glowR, cy - glowR * 0.3f}, {glowR * 2, glowR * 0.6f},
               {0.05f, 0.1f, 0.2f, 0.15f});
    batch.SetTexture(nullptr);
    batch.SetBlendMode(false);

    // Falling background notes
    ctx.piano->GetNoteTex(); // ensure texture exists
    batch.SetTexture(ctx.piano->GetNoteTex());
    for (auto& n : m_bgNotes) {
        batch.Draw({n.x, n.y}, {n.width, n.height},
                   {n.color.r, n.color.g, n.color.b, n.alpha * 0.3f});
    }
    batch.SetTexture(nullptr);
}

void MainMenuState::DrawTitle(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    auto& font = *ctx.font;
    int vw = ctx.window->Width();

    // Title with subtle float animation
    float titleY = 80.0f + std::sinf(m_titleAnim * 1.5f) * 5.0f;
    const char* title = "midiRoll";
    float tw = font.GetTextWidth(title, 2.0f);
    float tx = (vw - tw) * 0.5f;

    // Glow behind title
    batch.SetBlendMode(true);
    batch.Draw({tx - 20, titleY - 10}, {tw + 40, 60}, {0.2f, 0.5f, 1.0f, 0.08f});
    batch.SetBlendMode(false);

    font.DrawText(batch, title, tx, titleY, 0.4f, 0.75f, 1.0f, 2.0f);

    // Subtitle
    const char* sub = "A MIDI Piano Visualizer";
    float sw = font.GetTextWidth(sub, 0.85f);
    font.DrawText(batch, sub, (vw - sw) * 0.5f, titleY + 58, 0.55f, 0.55f, 0.65f, 0.85f);
}

void MainMenuState::DrawButtons(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    auto& font = *ctx.font;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    float btnW = 320.0f, btnH = 56.0f;
    float gap = 16.0f;
    float totalH = m_items.size() * btnH + (m_items.size() - 1) * gap;
    float startY = (vh - totalH) * 0.5f + 40.0f;

    auto drawRounded = [&](float x, float y, float w, float h, util::Color c) {
        batch.SetTexture(ctx.piano->GetNoteTex());
        float cap = h * 0.5f;
        if (cap > w * 0.5f) cap = w * 0.5f;
        batch.Draw({x, y}, {w, cap}, {c.r, c.g, c.b, c.a}, {0.0f, 0.0f}, {1.0f, 0.5f});
        if (h > cap * 2) batch.Draw({x, y + cap}, {w, h - cap * 2}, {c.r, c.g, c.b, c.a}, {0.0f, 0.49f}, {1.0f, 0.02f});
        batch.Draw({x, y + h - cap}, {w, cap}, {c.r, c.g, c.b, c.a}, {0.0f, 0.5f}, {1.0f, 0.5f});
        batch.SetTexture(nullptr);
    };

    for (int i = 0; i < (int)m_items.size(); i++) {
        float y = startY + i * (btnH + gap);
        float x = (vw - btnW) * 0.5f;
        bool isSel = (i == m_selected);
        float h = m_items[i].hoverAnim;

        // Button background
        util::Color bg = {0.06f + h * 0.06f, 0.06f + h * 0.08f, 0.10f + h * 0.12f, 0.9f};
        drawRounded(x, y, btnW, btnH, bg);

        // Selection indicator (left bar)
        if (isSel) {
            batch.Draw({x + 6, y + 8}, {3.0f, btnH - 16},
                       {m_items[i].color.r, m_items[i].color.g, m_items[i].color.b, 0.9f});
        }

        // Hover glow
        if (h > 0.01f) {
            batch.SetBlendMode(true);
            batch.SetTexture(ctx.piano->GetGradientTex());
            drawRounded(x, y, btnW, btnH,
                        {m_items[i].color.r * h * 0.15f,
                         m_items[i].color.g * h * 0.15f,
                         m_items[i].color.b * h * 0.15f, h * 0.3f});
            batch.SetTexture(nullptr);
            batch.SetBlendMode(false);
        }

        // Label
        float labelW = font.GetTextWidth(m_items[i].label, 0.95f);
        float lx = x + (btnW - labelW) * 0.5f;
        float ly = y + (btnH - 22) * 0.5f;
        util::Color c = m_items[i].color;
        float bright = isSel ? 1.0f : (0.75f + h * 0.25f);
        font.DrawText(batch, m_items[i].label, lx, ly,
                      c.r * bright, c.g * bright, c.b * bright, 0.95f);
    }
}

void MainMenuState::DrawHint(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    const char* hint = "Arrow keys / mouse to navigate  |  Enter to select";
    float hw = font.GetTextWidth(hint, 0.72f);
    font.DrawText(batch, hint, (vw - hw) * 0.5f, vh - 40.0f, 0.55f, 0.55f, 0.65f, 0.72f);
}

void MainMenuState::SpawnBackgroundNote(Context& ctx) {
    int vw = ctx.window->Width();
    std::uniform_real_distribution<float> distX(0, (float)vw);
    std::uniform_real_distribution<float> distSpeed(40.0f, 120.0f);
    std::uniform_real_distribution<float> distW(8.0f, 20.0f);
    std::uniform_real_distribution<float> distH(20.0f, 60.0f);
    std::uniform_real_distribution<float> distAlpha(0.4f, 0.8f);

    FallingNote n;
    n.x = distX(m_rng);
    n.y = -80.0f;
    n.speed = distSpeed(m_rng);
    n.width = distW(m_rng);
    n.height = distH(m_rng);
    n.color = util::ChannelColor(std::uniform_int_distribution<int>(0, 15)(m_rng));
    n.alpha = distAlpha(m_rng);
    
    // Occasionally spawn wider "chord" notes for visual variety
    if (std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.15f) {
        n.width *= 2.5f;
        n.height *= 1.5f;
        n.alpha *= 0.7f;
    }
    
    m_bgNotes.push_back(n);

    // Cap count
    if (m_bgNotes.size() > 80) m_bgNotes.erase(m_bgNotes.begin());
}

// ---------------------------------------------------------------------------
// Input - shared action handler
// ---------------------------------------------------------------------------

Transition MainMenuState::OnKey(Context& ctx, int key, bool down) {
    if (!down) return {};

    int count = (int)m_items.size();

    if (key == VK_UP) {
        m_selected = (m_selected - 1 + count) % count;
        // TODO: Play subtle hover sound effect here
        return Transition::Handled();
    }
    if (key == VK_DOWN) {
        m_selected = (m_selected + 1) % count;
        // TODO: Play subtle hover sound effect here
        return Transition::Handled();
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        // TODO: Play select sound effect here
        switch (m_items[m_selected].action) {
        case MenuAction::FreePlay:    return {StateID::FreePlay,     true, true};
        case MenuAction::MidiPlayback:return {StateID::MidiPlayback, true, true};
        case MenuAction::SoundFont:   OpenSoundFontDialog(ctx); return Transition::Handled();
        case MenuAction::MidiDevice:  CycleMidiDevice(ctx); return Transition::Handled();
        case MenuAction::Quit:        ctx.window->RequestClose(); return Transition::Handled();
        default: break;
        }
    }
    if (key == VK_ESCAPE) return Transition::Handled();

    return {};
}

Transition MainMenuState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    float btnW = 320.0f, btnH = 56.0f, gap = 16.0f;
    float totalH = m_items.size() * btnH + (m_items.size() - 1) * gap;
    float startY = (vh - totalH) * 0.5f + 40.0f;
    float btnX = (vw - btnW) * 0.5f;

    m_hovered = -1;
    for (int i = 0; i < (int)m_items.size(); i++) {
        float by = startY + i * (btnH + gap);
        if (x >= btnX && x <= btnX + btnW && y >= by && y <= by + btnH) {
            m_hovered = i;
            if (move) m_selected = i;
            break;
        }
    }

    if (down && m_hovered >= 0) {
        switch (m_items[m_hovered].action) {
        case MenuAction::FreePlay:    return {StateID::FreePlay,     true, true};
        case MenuAction::MidiPlayback:return {StateID::MidiPlayback, true, true};
        case MenuAction::SoundFont:   OpenSoundFontDialog(ctx); return Transition::Handled();
        case MenuAction::MidiDevice:  CycleMidiDevice(ctx); return Transition::Handled();
        case MenuAction::Quit:        ctx.window->RequestClose(); return Transition::Handled();
        default: break;
        }
    }

    return {};
}

} // namespace pfd
