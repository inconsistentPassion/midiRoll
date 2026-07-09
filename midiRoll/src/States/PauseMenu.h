#pragma once
#include "AppState.h"
#include "../Renderer/UIRenderer.h"
#include "../Renderer/FrostedGlassTheme.h"
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

    // ── Frosted Glass render ──
    void RenderGlass(UIRenderer& ui, int viewW, int viewH,
                     ID3D11ShaderResourceView* /*sceneSRV*/, float /*maxLOD*/) {
        if (!m_open) return;

        auto& T = glass::GetTheme();

        // Dimming overlay
        ui.DrawRect(0, 0, (float)viewW, (float)viewH, {T.bgColor.x, T.bgColor.y, T.bgColor.z, 0.6f});

        // Panel dimensions
        float btnW = 340.0f, btnH = 40.0f;
        float gap = 8.0f;
        float padding = 60.0f;
        float headerH = 50.0f;
        float footerH = 35.0f;
        float contentH = m_items.size() * btnH + (m_items.size() - 1) * gap;
        float panelH = padding + headerH + contentH + footerH;
        float panelW = 420.0f;
        float panelX = (viewW - panelW) * 0.5f;
        float panelY = (viewH - panelH) * 0.5f;

        // ── Glass panel ──
        UIRenderer::RectStyle panelStyle;
        panelStyle.color = util::Vec4(0, 0, 0, 0);
        panelStyle.cornerRadius = T.radiusXl;
        panelStyle.borderWidth = 1.0f;
        panelStyle.borderColor = util::Vec4(1, 1, 1, T.glassBorderAlpha);
        ui.DrawGlass(panelX, panelY, panelW, panelH, panelStyle, T.blurLOD, T.glassAlpha);

        // Header
        ui.DrawText("PAUSED", panelX + 30, panelY + 20, T.textPrimary, 1.1f);
        ui.DrawRect(panelX + 30, panelY + 55, panelW - 60, 1,
                    {T.accentBlue.x, T.accentBlue.y, T.accentBlue.z, 0.3f}, 0.5f);

        // Buttons
        float startY = panelY + 70.0f;
        float btnX = panelX + (panelW - btnW) * 0.5f;

        for (int i = 0; i < (int)m_items.size(); i++) {
            float y = startY + i * (btnH + gap);
            float x = btnX;
            bool isSel = (i == m_selected);
            float h = m_items[i].hoverAnim;
            float liftY = h * T.hoverLiftY;

            // ── Glass button ──
            UIRenderer::RectStyle bs;
            bs.color = util::Vec4(0, 0, 0, 0);
            bs.cornerRadius = T.radiusMd;
            bs.borderWidth = 1.0f;
            float borderA = T.glassBorderAlpha + h * (T.glassBorderHoverAlpha - T.glassBorderAlpha);
            if (isSel || h > 0.01f) {
                bs.borderColor = util::Vec4(
                    m_items[i].color.r * (0.3f + h * 0.5f),
                    m_items[i].color.g * (0.3f + h * 0.5f),
                    m_items[i].color.b * (0.3f + h * 0.5f),
                    borderA
                );
            } else {
                bs.borderColor = util::Vec4(1, 1, 1, borderA);
            }
            bs.glowSize = isSel ? 10.0f : (h * 6.0f);
            bs.glowIntensity = isSel ? 0.25f : (h * 0.15f);

            float glassA = T.glassAlpha + h * (T.glassHoverAlpha - T.glassAlpha);
            ui.DrawGlass(x, y + liftY, btnW, btnH, bs, T.blurLOD, glassA);

            // Selection accent bar
            if (isSel) {
                ui.DrawRect(x + 6, y + liftY + 8, 3, btnH - 16,
                            {m_items[i].color.r, m_items[i].color.g, m_items[i].color.b, 0.9f}, 1.5f);
            }

            // Label — centered
            float labelW = ui.GetTextWidth(m_items[i].label.c_str(), 0.8f);
            float lx = x + (btnW - labelW) * 0.5f;
            float ly = y + liftY + (btnH - ui.GetLineHeight(0.8f)) * 0.5f;
            float bright = isSel ? 1.0f : (0.75f + h * 0.25f);
            ui.DrawText(m_items[i].label.c_str(), lx, ly,
                        {m_items[i].color.r * bright, m_items[i].color.g * bright,
                         m_items[i].color.b * bright, 0.8f}, 0.8f);
        }

        // Keyboard shortcuts hint
        float footerY = panelY + panelH - 30;
        const char* hint = "Up/Down Navigate  |  Enter Select  |  Esc Back";
        float hw = ui.GetTextWidth(hint, 0.55f);
        ui.DrawText(hint, panelX + (panelW - hw) * 0.5f, footerY, T.textMuted, 0.55f);
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
