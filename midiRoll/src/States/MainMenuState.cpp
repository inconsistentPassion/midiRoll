#include "MainMenuState.h"
#include <algorithm>
#include <cmath>
#include <Windows.h>
#include <commdlg.h>

namespace pfd {

void MainMenuState::Enter(Context& ctx) {
    m_selected = 0;
    m_hovered = -1;
    m_bgNotes.clear();
    m_spawnTimer = 0;
    m_titleAnim = 0;
    m_enterAnim = 1.0f;
    ctx.audio->AllNotesOff();
    ctx.input->ClearEvents();
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
    float sw = font.GetTextWidth(sub, 0.7f);
    font.DrawText(batch, sub, (vw - sw) * 0.5f, titleY + 55, 0.4f, 0.4f, 0.5f, 0.7f);
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
        float labelW = font.GetTextWidth(m_items[i].label, 0.85f);
        float lx = x + (btnW - labelW) * 0.5f;
        float ly = y + (btnH - 20) * 0.5f;
        util::Color c = m_items[i].color;
        float bright = isSel ? 1.0f : (0.7f + h * 0.3f);
        font.DrawText(batch, m_items[i].label, lx, ly,
                      c.r * bright, c.g * bright, c.b * bright, 0.85f);
    }
}

void MainMenuState::DrawHint(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    const char* hint = "Arrow keys to navigate  |  Enter to select";
    float hw = font.GetTextWidth(hint, 0.55f);
    font.DrawText(batch, hint, (vw - hw) * 0.5f, vh - 40.0f, 0.3f, 0.3f, 0.4f, 0.55f);
}

void MainMenuState::SpawnBackgroundNote(Context& ctx) {
    int vw = ctx.window->Width();
    std::uniform_real_distribution<float> distX(0, (float)vw);
    std::uniform_real_distribution<float> distSpeed(40.0f, 120.0f);
    std::uniform_real_distribution<float> distW(8.0f, 20.0f);
    std::uniform_real_distribution<float> distH(20.0f, 60.0f);

    FallingNote n;
    n.x = distX(m_rng);
    n.y = -80.0f;
    n.speed = distSpeed(m_rng);
    n.width = distW(m_rng);
    n.height = distH(m_rng);
    n.color = util::ChannelColor(std::uniform_int_distribution<int>(0, 15)(m_rng));
    n.alpha = 0.6f;
    m_bgNotes.push_back(n);

    // Cap count
    if (m_bgNotes.size() > 80) m_bgNotes.erase(m_bgNotes.begin());
}

Transition MainMenuState::OnKey(Context& ctx, int key, bool down) {
    (void)ctx;
    if (!down) return {};

    int count = (int)m_items.size();

    if (key == VK_UP) {
        m_selected = (m_selected - 1 + count) % count;
        return Transition::Handled();
    }
    if (key == VK_DOWN) {
        m_selected = (m_selected + 1) % count;
        return Transition::Handled();
    }
    if (key == VK_RETURN || key == VK_SPACE) {
        const std::string& label = m_items[m_selected].label;
        if (label == "SOUNDFONT") {
            wchar_t filePath[MAX_PATH] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ctx.window->Handle();
            ofn.lpstrFilter = L"SoundFont Files (*.sf2)\0*.sf2\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Open SoundFont File";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                int nlen = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
                std::string narrow(nlen - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, filePath, -1, narrow.data(), nlen, nullptr, nullptr);
                ctx.audio->LoadSoundFont(narrow);
                ctx.soundFontPath = narrow;
            }
            return Transition::Handled();
        }
        if (label == "QUIT") {
            ctx.window->RequestClose();
            return Transition::Handled();
        }
        return {m_items[m_selected].target, true, true};
    }
    if (key == VK_ESCAPE) {
        // Do nothing - ESC goes back to menu only
        return Transition::Handled();
    }

    return {};
}

Transition MainMenuState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    (void)ctx;
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
        const std::string& label = m_items[m_hovered].label;
        if (label == "SOUNDFONT") {
            wchar_t filePath[MAX_PATH] = {};
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ctx.window->Handle();
            ofn.lpstrFilter = L"SoundFont Files (*.sf2)\0*.sf2\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = L"Open SoundFont File";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                int nlen = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
                std::string narrow(nlen - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, filePath, -1, narrow.data(), nlen, nullptr, nullptr);
                ctx.audio->LoadSoundFont(narrow);
                ctx.soundFontPath = narrow;
            }
            return Transition::Handled();
        }
        if (label == "QUIT") {
            ctx.window->RequestClose();
            return Transition::Handled();
        }
        return {m_items[m_hovered].target, true, true};
    }

    return {};
}

} // namespace pfd
