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
    ChangeBackground,
    OpenMidiFile,
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
                int viewW, int viewH, ID3D11ShaderResourceView* screenTex = nullptr) {
        if (!m_open) return;

        if (screenTex) {
            batch.SetTexture(screenTex);
            batch.SetBlendMode(false);
            // Draw opaque center to cover the sharp scene behind
            batch.Draw({0, 0}, {(float)viewW, (float)viewH}, {1,1,1,1});
            
            // Draw offsets to create a cheap box blur
            float offsets[] = { -6.0f, -3.0f, 3.0f, 6.0f };
            for (float ox : offsets) {
                for (float oy : offsets) {
                    batch.Draw({ox, oy}, {(float)viewW, (float)viewH}, {1,1,1,0.06f});
                }
            }
        }

        batch.SetTexture(nullptr);
        // Dark semi-transparent overlay
        batch.Draw({0, 0}, {(float)viewW, (float)viewH}, {0.0f, 0.0f, 0.02f, 0.6f});

        // Calculate panel height dynamically based on items
        float btnW = 340.0f, btnH = 40.0f;
        float gap = 8.0f;
        float padding = 60.0f; // top + bottom padding
        float headerH = 50.0f;
        float footerH = 35.0f; // space for keyboard hints
        float contentH = m_items.size() * btnH + (m_items.size() - 1) * gap;
        float panelH = padding + headerH + contentH + footerH;
        float panelW = 420.0f;
        float panelX = (viewW - panelW) * 0.5f;
        float panelY = (viewH - panelH) * 0.5f;

        // Panel background with subtle gradient
        drawRounded(batch, noteTex, panelX, panelY, panelW, panelH, {0.04f, 0.04f, 0.07f, 0.98f});
        
        // Add inner glow for depth
        batch.SetBlendMode(true);
        drawRounded(batch, noteTex, panelX + 2, panelY + 2, panelW - 4, panelH - 4, 
                   {0.1f, 0.15f, 0.25f, 0.1f});
        batch.SetBlendMode(false);

        // Header with decorative line
        font.DrawText(batch, "PAUSED", panelX + 30, panelY + 20, 0.6f, 0.85f, 1.0f, 1.1f);
        batch.Draw({panelX + 30, panelY + 55}, {panelW - 60, 1}, {0.3f, 0.5f, 0.8f, 0.3f});

        // Buttons
        float startY = panelY + 70.0f;
        float btnX = panelX + (panelW - btnW) * 0.5f; // center buttons in panel

        for (int i = 0; i < (int)m_items.size(); i++) {
            float y = startY + i * (btnH + gap);
            float x = btnX;
            bool isSel = (i == m_selected);
            float h = m_items[i].hoverAnim;

            // Button bg with hover effect
            util::Color bg = {0.06f + h * 0.06f, 0.06f + h * 0.08f, 0.10f + h * 0.12f, 0.9f};
            drawRounded(batch, noteTex, x, y, btnW, btnH, bg);

            // Selection bar with glow
            if (isSel) {
                batch.SetBlendMode(true);
                batch.Draw({x + 6, y + 8}, {3.0f, btnH - 16},
                           {m_items[i].color.r, m_items[i].color.g, m_items[i].color.b, 1.0f});
                batch.Draw({x + 6, y + 8}, {3.0f, btnH - 16},
                           {m_items[i].color.r * 0.5f, m_items[i].color.g * 0.5f, m_items[i].color.b * 0.5f, 0.5f});
                batch.SetBlendMode(false);
            }

            // Hover glow
            if (h > 0.01f) {
                batch.SetBlendMode(true);
                drawRounded(batch, noteTex, x, y, btnW, btnH,
                            {m_items[i].color.r * h * 0.15f, m_items[i].color.g * h * 0.15f,
                             m_items[i].color.b * h * 0.15f, h * 0.3f});
                batch.SetBlendMode(false);
            }

            // Label - centered
            float labelW = font.GetTextWidth(m_items[i].label.c_str(), 0.8f);
            float lx = x + (btnW - labelW) * 0.5f;
            float ly = y + (btnH - 18) * 0.5f;
            util::Color c = m_items[i].color;
            float bright = isSel ? 1.0f : (0.75f + h * 0.25f);
            font.DrawText(batch, m_items[i].label.c_str(), lx, ly,
                          c.r * bright, c.g * bright, c.b * bright, 0.8f);
        }

        // Keyboard shortcuts hint at bottom
        float footerY = panelY + panelH - 30;
        const char* hint = "\u2191\u2193 Navigate  |  Enter Select  |  Esc Back";
        float hw = font.GetTextWidth(hint, 0.55f);
        font.DrawText(batch, hint, panelX + (panelW - hw) * 0.5f, footerY,
                      0.45f, 0.45f, 0.55f, 0.55f);
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
            // TODO: Play subtle hover sound effect here
            return PauseAction::None;
        }
        if (key == VK_DOWN) {
            m_selected = (m_selected + 1) % count;
            // TODO: Play subtle hover sound effect here
            return PauseAction::None;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            // TODO: Play select sound effect here
            return m_items[m_selected].action;
        }

        return PauseAction::None;
    }

    // Handle mouse input. Returns the selected action.
    PauseAction OnMouse(int x, int y, bool down, bool move, int viewW, int viewH) {
        if (!m_open) return PauseAction::None;

        float btnW = 340.0f, btnH = 40.0f;
        float gap = 8.0f;
        float padding = 60.0f; // top + bottom padding
        float headerH = 50.0f;
        float footerH = 35.0f; // space for keyboard hints
        float contentH = m_items.size() * btnH + (m_items.size() - 1) * gap;
        float panelH = padding + headerH + contentH + footerH;
        float panelW = 420.0f;
        float panelX = (viewW - panelW) * 0.5f;
        float panelY = (viewH - panelH) * 0.5f;

        float startY = panelY + 70.0f;
        float btnX = panelX + (panelW - btnW) * 0.5f; // center buttons in panel

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
        {"OPEN MIDI...",     PauseAction::OpenMidiFile,    {0.4f, 0.9f, 0.6f, 1.0f}},
        {"SOUNDFONT...",     PauseAction::ChangeSoundFont, {0.8f, 0.7f, 0.4f, 1.0f}},
        {"MODE: FALLING",    PauseAction::ToggleMode,      {0.7f, 0.4f, 0.8f, 1.0f}},
        {"BACKGROUND...",    PauseAction::ChangeBackground, {0.3f, 0.8f, 0.8f, 1.0f}},
        {"MAIN MENU",        PauseAction::BackToMenu,      {0.5f, 0.5f, 0.6f, 1.0f}},
        {"QUIT",             PauseAction::Quit,            {0.8f, 0.3f, 0.3f, 1.0f}},
    }};
};

} // namespace pfd
