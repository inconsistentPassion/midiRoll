#pragma once
#include "AppState.h"
#include "../Util/Color.h"
#include <array>

namespace pfd {

enum class PauseAction {
    None,
    Resume,
    ToggleSpeed,
    ChangeSoundFont,
    ToggleMode,
    BackToMenu,
    Quit,
};

class PauseMenu {
public:
    void Open() { m_open = true; m_selected = 0; m_hovered = -1; }
    void Close() { m_open = false; }
    bool IsOpen() const { return m_open; }

    void SetLabel(int index, const std::string& label) {
        if (index >= 0 && index < (int)m_items.size()) {
            m_items[index].label = label;
        }
    }

    // Returns the action if an item was selected
    PauseAction Update(double dt) {
        if (!m_open) return PauseAction::None;

        for (int i = 0; i < (int)m_items.size(); i++) {
            float target = (i == m_hovered) ? 1.0f : 0.0f;
            m_items[i].hoverAnim += (target - m_items[i].hoverAnim) * std::min(1.0f, (float)dt * 12.0f);
        }
        return PauseAction::None;
    }

    void Render(SpriteBatch& batch, FontRenderer& font, ID3D11ShaderResourceView* noteTex,
                int viewW, int viewH) {
        if (!m_open) return;

        // Dark overlay
        batch.Draw({0, 0}, {(float)viewW, (float)viewH}, {0.0f, 0.0f, 0.02f, 0.7f});

        // Panel
        float panelW = 400.0f, panelH = 420.0f;
        float panelX = (viewW - panelW) * 0.5f;
        float panelY = (viewH - panelH) * 0.5f;

        // Panel background
        drawRounded(batch, noteTex, panelX, panelY, panelW, panelH, {0.05f, 0.05f, 0.08f, 0.97f});

        // Header
        font.DrawText(batch, "PAUSED", panelX + 30, panelY + 20, 0.5f, 0.7f, 1.0f, 1.0f);

        // Buttons
        float btnW = panelW - 60.0f, btnH = 40.0f;
        float startY = panelY + 70.0f;
        float gap = 8.0f;

        for (int i = 0; i < (int)m_items.size(); i++) {
            float y = startY + i * (btnH + gap);
            float x = panelX + 30.0f;
            bool isSel = (i == m_selected);
            float h = m_items[i].hoverAnim;

            // Button bg
            util::Color bg = {0.06f + h * 0.06f, 0.06f + h * 0.08f, 0.10f + h * 0.12f, 0.9f};
            drawRounded(batch, noteTex, x, y, btnW, btnH, bg);

            // Selection bar
            if (isSel) {
                batch.Draw({x + 6, y + 8}, {3.0f, btnH - 16},
                           {m_items[i].color.r, m_items[i].color.g, m_items[i].color.b, 0.9f});
            }

            // Hover glow
            if (h > 0.01f) {
                batch.SetBlendMode(true);
                drawRounded(batch, noteTex, x, y, btnW, btnH,
                            {m_items[i].color.r * h * 0.15f, m_items[i].color.g * h * 0.15f,
                             m_items[i].color.b * h * 0.15f, h * 0.3f});
                batch.SetBlendMode(false);
            }

            // Label
            float labelW = font.GetTextWidth(m_items[i].label.c_str(), 0.6f);
            float lx = x + (btnW - labelW) * 0.5f;
            float ly = y + (btnH - 16) * 0.5f;
            util::Color c = m_items[i].color;
            float bright = isSel ? 1.0f : (0.7f + h * 0.3f);
            font.DrawText(batch, m_items[i].label.c_str(), lx, ly,
                          c.r * bright, c.g * bright, c.b * bright, 0.6f);
        }
    }

    // Handle key input. Returns the selected action.
    PauseAction OnKey(int key, bool down) {
        if (!m_open || !down) return PauseAction::None;

        if (key == VK_ESCAPE) {
            m_open = false;
            return PauseAction::Resume;
        }

        int count = (int)m_items.size();
        if (key == VK_UP) {
            m_selected = (m_selected - 1 + count) % count;
            return PauseAction::None;
        }
        if (key == VK_DOWN) {
            m_selected = (m_selected + 1) % count;
            return PauseAction::None;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            return m_items[m_selected].action;
        }

        return PauseAction::None;
    }

    // Handle mouse input. Returns the selected action.
    PauseAction OnMouse(int x, int y, bool down, bool move, int viewW, int viewH) {
        if (!m_open) return PauseAction::None;

        float panelW = 400.0f, panelH = 420.0f;
        float panelX = (viewW - panelW) * 0.5f;
        float panelY = (viewH - panelH) * 0.5f;
        float btnW = panelW - 60.0f, btnH = 40.0f;
        float startY = panelY + 70.0f;
        float gap = 8.0f;
        float btnX = panelX + 30.0f;

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
            return m_items[m_hovered].action;
        }

        return PauseAction::None;
    }

private:
    struct MenuItem {
        std::string label;
        PauseAction action;
        util::Color color;
        float hoverAnim{};
    };

    void drawRounded(SpriteBatch& batch, ID3D11ShaderResourceView* tex,
                     float x, float y, float w, float h, util::Color c) {
        batch.SetTexture(tex);
        float cap = h * 0.5f;
        if (cap > w * 0.5f) cap = w * 0.5f;
        batch.Draw({x, y}, {w, cap}, {c.r, c.g, c.b, c.a}, {0.0f, 0.0f}, {1.0f, 0.5f});
        if (h > cap * 2) batch.Draw({x, y + cap}, {w, h - cap * 2}, {c.r, c.g, c.b, c.a}, {0.0f, 0.49f}, {1.0f, 0.02f});
        batch.Draw({x, y + h - cap}, {w, cap}, {c.r, c.g, c.b, c.a}, {0.0f, 0.5f}, {1.0f, 0.5f});
        batch.SetTexture(nullptr);
    }

    bool m_open{};
    int m_selected{};
    int m_hovered{-1};

    std::vector<MenuItem> m_items{{
        {"RESUME",           PauseAction::Resume,          {0.4f, 0.8f, 0.5f, 1.0f}},
        {"SPEED: 1.0x",      PauseAction::ToggleSpeed,     {0.4f, 0.7f, 1.0f, 1.0f}},
        {"SOUNDFONT...",     PauseAction::ChangeSoundFont, {0.8f, 0.7f, 0.4f, 1.0f}},
        {"MODE: FALLING",    PauseAction::ToggleMode,      {0.7f, 0.4f, 0.8f, 1.0f}},
        {"MAIN MENU",        PauseAction::BackToMenu,      {0.5f, 0.5f, 0.6f, 1.0f}},
        {"QUIT",             PauseAction::Quit,            {0.8f, 0.3f, 0.3f, 1.0f}},
    }};
};

} // namespace pfd
