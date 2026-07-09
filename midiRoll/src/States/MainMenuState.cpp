#include "MainMenuState.h"
#include "StateHelpers.h"
#include "../Input/MidiInput.h"
#include <algorithm>
#include <cmath>
#include <fstream>

#define NOMINMAX
#include <Windows.h>
#undef DrawText
#include <commdlg.h>
#include <shellapi.h>

namespace pfd {

// ═════════════════════════════════════════════════════════════════════════════
// Layout constants (matching the gacha-style concept)
// ═════════════════════════════════════════════════════════════════════════════

namespace {
    // Frosted Glass theme colors
    auto& T = glass::GetTheme();

    // Button accent colors (kept for identity, applied as tinted glass)
    const util::Vec4 COL_FREEPLAY    = {0.3f, 0.8f, 1.0f, 1.0f};
    const util::Vec4 COL_OPENMIDI    = {1.0f, 0.6f, 0.2f, 1.0f};
    const util::Vec4 COL_SOUNDFONT   = {0.8f, 0.7f, 0.4f, 1.0f};
    const util::Vec4 COL_MIDIDEV     = {0.3f, 0.8f, 0.8f, 1.0f};
    const util::Vec4 COL_SETTINGS    = {0.7f, 0.7f, 0.7f, 1.0f};
}

// ═════════════════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════════════════

void MainMenuState::Enter(Context& ctx) {
    m_focused = ButtonID::FreePlay;
    m_bgNotes.clear();
    m_spawnTimer = 0;
    m_titleAnim = 0;
    m_enterAnim = 1.0f;
    m_lastW = m_lastH = 0;
    m_projectScrollOffset = 0.0f;
    ctx.audio->AllNotesOff();
    ctx.input->ClearEvents();

    // Sync MIDI button label
    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        m_midiDeviceIndex = ctx.midiInput->DeviceIndex();
    } else {
        m_midiDeviceIndex = -1;
    }

    // Load recent projects from disk
    LoadRecentProjects();
}

void MainMenuState::Exit(Context& ctx) {
    (void)ctx;
}

Transition MainMenuState::Update(Context& ctx, double dt) {
    m_titleAnim += (float)dt;
    m_enterAnim = std::max(0.0f, m_enterAnim - (float)dt * 2.0f);

    // Smooth hover animations for all buttons
    for (auto& btn : m_buttons) {
        float target = btn.hovered ? 1.0f : 0.0f;
        btn.hoverAnim += (target - btn.hoverAnim) * std::min(1.0f, (float)dt * 12.0f);
        
        // Decay press animation
        if (!btn.hovered) {
            btn.pressAnim += (0.0f - btn.pressAnim) * std::min(1.0f, (float)dt * 15.0f);
        }
    }

    // Smooth hover animations for project cards
    for (auto& proj : m_recentProjects) {
        float target = proj.hovered ? 1.0f : 0.0f;
        proj.hoverAnim += (target - proj.hoverAnim) * std::min(1.0f, (float)dt * 10.0f);
    }

    // Spawn background notes
    m_spawnTimer += (float)dt;
    if (m_spawnTimer > 0.12f) {
        m_spawnTimer = 0;
        SpawnBackgroundNote(ctx);
    }

    // Update background notes
    float viewH = (float)ctx.window->Height();
    for (auto& n : m_bgNotes) {
        n.y += n.speed * (float)dt;
        n.alpha -= 0.12f * (float)dt;
    }
    m_bgNotes.erase(
        std::remove_if(m_bgNotes.begin(), m_bgNotes.end(),
            [&](const FallingNote& n) { return n.y > viewH || n.alpha <= 0; }),
        m_bgNotes.end());

    return {};
}

// ═════════════════════════════════════════════════════════════════════════════
// Rendering — all via UIRenderer, zero SpriteBatch
// ═════════════════════════════════════════════════════════════════════════════

void MainMenuState::Render(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Rebuild layout on resize
    if (vw != m_lastW || vh != m_lastH) RebuildLayout(vw, vh);

    // ── Phase 1: Render background (solid base + falling notes + glow) ──
    // This is the "game world" that glass will refract.
    ui.Begin(ctx.d3d->Context(), vw, vh);
    DrawBackground(ctx);
    ui.End();

    // ── Phase 2: Capture scene + generate mipmaps for glass blur ──
    // The mipmap-stacking approach: each mip level is already a blurred
    // version of the previous. SampleLevel(scene, uv, 3.0) = 1/8 res blur.
    // Cost: ~0.05ms. All glass elements share ONE generation per frame.
    auto* sceneSRV = ctx.d3d->CaptureSceneForGlass();
    float maxLOD = ctx.d3d->GetSceneMaxLOD();

    // ── Phase 3: Render background blobs (colored circles for glass to refract) ──
    // Glass over solid black is invisible. Blobs provide color.
    ctx.glass->Render(ctx.d3d->Context(), vw, vh, m_titleAnim);

    // ── Phase 4: Render glass UI ──
    // Begin with scene texture for glass blur
    ui.Begin(ctx.d3d->Context(), vw, vh, sceneSRV, maxLOD);

    DrawRecentProjects(ctx);
    DrawMainButtons(ctx);
    DrawTitle(ctx);
    DrawBottomBar(ctx);
    DrawHint(ctx);

    // Fade-in overlay
    if (m_enterAnim > 0.01f) {
        ui.DrawRect(0, 0, (float)vw, (float)vh,
                    {T.bgColor.x, T.bgColor.y, T.bgColor.z, m_enterAnim});
    }

    ui.End();
}

// ── Background ──────────────────────────────────────────────────────────────

void MainMenuState::DrawBackground(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Full-screen dark base (Frosted Glass near-black)
    ui.DrawRect(0, 0, (float)vw, (float)vh, T.bgColor);

    // Falling background notes (colored rectangles — provide color for glass to refract)
    for (auto& n : m_bgNotes) {
        UIRenderer::RectStyle ns;
        ns.color = {n.color.r, n.color.g, n.color.b, n.alpha * 0.25f};
        ns.cornerRadius = std::min(n.width, n.height) * 0.3f;
        ns.glowSize = 6.0f;
        ns.glowIntensity = n.alpha * 0.3f;
        ns.borderColor = {n.color.r, n.color.g, n.color.b, n.alpha * 0.15f};
        ui.DrawRect(n.x, n.y, n.width, n.height, ns);
    }
}

// ── Title ───────────────────────────────────────────────────────────────────

void MainMenuState::DrawTitle(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();

    float titleX = (float)vw * 0.06f;
    float titleY = 50.0f + std::sinf(m_titleAnim * 1.2f) * 4.0f;

    // "midiRoll" — large, with glow (Frosted Glass text hierarchy)
    ui.DrawTextWithGlow("midiRoll", titleX, titleY,
                        T.textPrimary, {0.2f, 0.5f, 1.0f, 0.8f},
                        6.0f, 0.5f, 3.0f);

    // Subtitle
    ui.DrawText("A MIDI Piano Visualizer",
                titleX, titleY + 52.0f, T.textSecondary, 0.8f);
}

// ── Main buttons (FREE PLAY + OPEN MIDI) ────────────────────────────────────

void MainMenuState::DrawMainButtons(Context& ctx) {
    auto& ui = *ctx.ui;

    for (int i = 0; i <= (int)ButtonID::OpenMidi; i++) {
        auto& btn = m_buttons[i];
        float h = btn.hoverAnim;
        float p = btn.pressAnim;
        bool focused = (m_focused == (ButtonID)i);

        // Press animation: scale down slightly when pressed
        float scale = 1.0f - p * T.pressScale + T.pressScale; // 0.97 on press
        float actualScale = 1.0f - p * 0.05f;
        float scaledW = btn.w * actualScale;
        float scaledH = btn.h * actualScale;
        float offsetX = (btn.w - scaledW) * 0.5f;
        float offsetY = (btn.h - scaledH) * 0.5f;

        // Hover lift (translateY -1px)
        float liftY = h * T.hoverLiftY;

        util::Vec4 accent = (i == 0) ? COL_FREEPLAY : COL_OPENMIDI;

        // ── Frosted Glass button ──
        // Glass material with accent-tinted border on hover/focus
        UIRenderer::RectStyle style;
        style.color = util::Vec4(0, 0, 0, 0); // glass handles its own fill
        style.cornerRadius = T.radiusMd;
        style.borderWidth = 1.0f;
        float borderA = T.glassBorderAlpha + h * (T.glassBorderHoverAlpha - T.glassBorderAlpha);
        if (focused || h > 0.01f) {
            style.borderColor = util::Vec4(
                accent.x * (0.3f + h * 0.5f),
                accent.y * (0.3f + h * 0.5f),
                accent.z * (0.3f + h * 0.5f),
                borderA
            );
        } else {
            style.borderColor = util::Vec4(1, 1, 1, borderA);
        }
        style.glowSize = focused ? 16.0f : (h * 8.0f);
        style.glowIntensity = focused ? 0.3f : (h * 0.2f);

        // Draw glass panel (mipmap blur from scene texture)
        float glassA = T.glassAlpha + h * (T.glassHoverAlpha - T.glassAlpha);
        ui.DrawGlass(btn.x + offsetX, btn.y + offsetY + liftY, scaledW, scaledH,
                     style, T.blurLOD, glassA);

        // Accent bar on left edge (for focused button)
        if (focused) {
            float barHeight = scaledH - 24;
            ui.DrawRect(btn.x + offsetX + 6, btn.y + offsetY + liftY + 12, 3, barHeight,
                        {accent.x, accent.y, accent.z, 0.9f}, 1.5f);
        }

        // Button label — centered with adaptive scaling
        const char* label = (i == 0) ? "FREE PLAY" : "OPEN MIDI";
        float textScale = 1.1f * actualScale;
        float tw = ui.GetTextWidth(label, textScale);
        float tx = btn.x + offsetX + (scaledW - tw) * 0.5f;
        float ty = btn.y + offsetY + liftY + (scaledH - ui.GetLineHeight(textScale)) * 0.5f;

        float bright = focused ? 1.0f : (0.8f + h * 0.2f);
        ui.DrawText(label, tx, ty,
                    {accent.x * bright, accent.y * bright, accent.z * bright, 0.95f},
                    textScale);
    }
}

// ── Recent Projects (right sidebar) ─────────────────────────────────────────

void MainMenuState::DrawRecentProjects(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Section header — Frosted Glass text hierarchy
    float headerX = (float)vw * 0.52f;
    float headerY = 55.0f;
    float headerScale = std::min(1.2f, std::max(0.8f, (float)vh / 1080.0f));
    ui.DrawText("Recent Projects", headerX, headerY, T.textPrimary, headerScale);

    // Responsive card sizing
    float cardX = headerX;
    float cardY = headerY + 45.0f * headerScale;
    float cardW = (float)vw * 0.42f;
    float baseCardH = 170.0f;
    float heightFactor = std::min(1.0f, std::max(0.7f, (float)vh / 1080.0f));
    float cardH = baseCardH * heightFactor;
    float cardGap = 14.0f;

    float availableHeight = (float)vh - cardY - 100.0f;
    m_maxVisibleProjects = std::max(2, std::min(5, (int)(availableHeight / (cardH + cardGap))));
    float visibleStartY = cardY - m_projectScrollOffset;

    for (int i = 0; i < (int)m_recentProjects.size(); i++) {
        auto& proj = m_recentProjects[i];
        float cy = visibleStartY + i * (cardH + cardGap);
        if (cy + cardH < 0 || cy > (float)vh) continue;

        // Hover expansion
        float expansion = proj.hoverAnim * 8.0f;
        float expandedW = cardW + expansion * 2;
        float expandedH = cardH + expansion;
        float expandedX = cardX - expansion;
        float expandedY = cy - expansion * 0.5f;

        // Hover lift
        float liftY = proj.hoverAnim * T.hoverLiftY;

        // ── Frosted Glass card ──
        UIRenderer::RectStyle cardStyle;
        cardStyle.color = util::Vec4(0, 0, 0, 0);
        cardStyle.cornerRadius = T.radiusMd;
        cardStyle.borderWidth = 1.0f;
        float borderA = T.glassBorderAlpha + proj.hoverAnim * (T.glassBorderHoverAlpha - T.glassBorderAlpha);
        cardStyle.borderColor = util::Vec4(1, 1, 1, borderA);
        cardStyle.glowSize = proj.hoverAnim * 12.0f;
        cardStyle.glowIntensity = proj.hoverAnim * 0.2f;

        float glassA = T.glassAlpha + proj.hoverAnim * (T.glassHoverAlpha - T.glassAlpha);
        ui.DrawGlass(expandedX, expandedY + liftY, expandedW, expandedH,
                     cardStyle, T.blurLOD, glassA);

        // Project name + date
        float textScale = std::min(0.85f, std::max(0.65f, heightFactor));
        float dateScale = std::min(0.6f, std::max(0.5f, heightFactor));
        ui.DrawText(proj.name, expandedX + 16, expandedY + liftY + 12,
                    T.textPrimary, textScale);
        ui.DrawText(proj.date, expandedX + 16, expandedY + liftY + 32,
                    T.textMuted, dateScale);

        // Mini piano roll preview
        float previewX = expandedX + 16;
        float previewY = expandedY + liftY + 55 * heightFactor;
        float previewW = expandedW - 32;
        float previewH = 80.0f * heightFactor;

        // Dark grid background
        ui.DrawRect(previewX, previewY, previewW, previewH,
                    {0.03f, 0.03f, 0.05f, 0.8f}, 8.0f);

        // Note blocks
        for (auto& note : proj.notes) {
            float nx = previewX + note.x * previewW;
            float ny = previewY + note.y * previewH;
            float nw = std::max(note.w * previewW, 3.0f);
            float nh = std::max(note.h * previewH, 3.0f);
            UIRenderer::RectStyle ns;
            ns.color = {note.color.r, note.color.g, note.color.b, 0.8f};
            ns.cornerRadius = 2.0f;
            ns.glowSize = 3.0f + proj.hoverAnim * 3.0f;
            ns.glowIntensity = 0.2f + proj.hoverAnim * 0.2f;
            ns.borderColor = {note.color.r * 0.5f, note.color.g * 0.5f, note.color.b * 0.5f, 0.3f};
            ui.DrawRect(nx, ny, nw, nh, ns);
        }

        // Project name at bottom
        ui.DrawText(proj.name, expandedX + 16, expandedY + liftY + expandedH - 22 * heightFactor,
                    T.textMuted, 0.55f * heightFactor);
    }

    // Scroll indicator
    if ((int)m_recentProjects.size() > m_maxVisibleProjects) {
        float scrollBarX = cardX + cardW + 5;
        float scrollBarY = cardY;
        float scrollBarH = (float)m_maxVisibleProjects * (cardH + cardGap);
        float scrollThumbH = scrollBarH * ((float)m_maxVisibleProjects / (float)m_recentProjects.size());
        float scrollThumbY = scrollBarY + m_projectScrollOffset * (scrollBarH / ((float)m_recentProjects.size() * (cardH + cardGap)));

        ui.DrawRect(scrollBarX, scrollBarY, 4, scrollBarH, {1, 1, 1, 0.08f}, 2.0f);
        ui.DrawRect(scrollBarX, scrollThumbY, 4, scrollThumbH, {1, 1, 1, 0.20f}, 2.0f);
    }

    // "VIEW ALL PROJECTS" link
    float linkY = visibleStartY + m_recentProjects.size() * (cardH + cardGap) + 5.0f;
    if (linkY < (float)vh - 80) {
        float linkW = ui.GetTextWidth("VIEW ALL PROJECTS", 0.7f * heightFactor);
        ui.DrawText("VIEW ALL PROJECTS", headerX + (cardW - linkW) * 0.5f, linkY,
                    T.textMuted, 0.7f * heightFactor);
    }
}

// ── Bottom bar ──────────────────────────────────────────────────────────────

void MainMenuState::DrawBottomBar(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    float barY = (float)vh - 65.0f;

    // GPU status (left) — Frosted Glass text hierarchy
    float statusScale = std::min(0.55f, std::max(0.45f, (float)vh / 1080.0f));
    ui.DrawText("GPU: NVIDIA RTX 4080", 20.0f, barY + 8, T.textMuted, statusScale);

    // Bottom buttons — Frosted Glass style
    float baseBtnW = 110.0f;
    float baseBtnH = 40.0f;
    float btnGap = 10.0f;
    float widthFactor = std::min(1.0f, std::max(0.8f, (float)vw / 1920.0f));
    float btnW = baseBtnW * widthFactor;
    float btnH = baseBtnH * widthFactor;

    float totalW = 3 * btnW + 2 * btnGap;
    float startX;
    if (totalW > vw * 0.6f) {
        btnW = (vw * 0.6f - 2 * btnGap) / 3;
        btnH = btnW * 0.36f;
        totalW = 3 * btnW + 2 * btnGap;
        startX = (float)vw - totalW - 20.0f;
    } else {
        startX = (float)vw - totalW - 20.0f;
    }

    const char* labels[] = {"SOUNDFONT", "MIDI DEVICE", "SETTINGS"};
    const util::Vec4 accents[] = {COL_SOUNDFONT, COL_MIDIDEV, COL_SETTINGS};
    ButtonID ids[] = {ButtonID::SoundFont, ButtonID::MidiDevice, ButtonID::Settings};

    for (int i = 0; i < 3; i++) {
        float bx = startX + i * (btnW + btnGap);
        int bi = (int)ids[i];
        float h = m_buttons[bi].hoverAnim;
        float p = m_buttons[bi].pressAnim;
        bool focused = (m_focused == ids[i]);

        float scale = 1.0f - p * 0.05f;
        float scaledW = btnW * scale;
        float scaledH = btnH * scale;
        float offsetX = (btnW - scaledW) * 0.5f;
        float offsetY = (btnH - scaledH) * 0.5f;
        float liftY = h * T.hoverLiftY;

        // ── Frosted Glass button ──
        UIRenderer::RectStyle bs;
        bs.color = util::Vec4(0, 0, 0, 0);
        bs.cornerRadius = T.radiusSm;
        bs.borderWidth = 1.0f;
        float borderA = T.glassBorderAlpha + h * (T.glassBorderHoverAlpha - T.glassBorderAlpha);
        if (focused || h > 0.01f) {
            bs.borderColor = util::Vec4(
                accents[i].x * (0.3f + h * 0.5f),
                accents[i].y * (0.3f + h * 0.5f),
                accents[i].z * (0.3f + h * 0.5f),
                borderA
            );
        } else {
            bs.borderColor = util::Vec4(1, 1, 1, borderA);
        }
        bs.glowSize = focused ? 10.0f : (h * 6.0f);
        bs.glowIntensity = focused ? 0.25f : (h * 0.15f);

        float glassA = T.glassAlpha + h * (T.glassHoverAlpha - T.glassAlpha);
        ui.DrawGlass(bx + offsetX, barY + offsetY + liftY, scaledW, scaledH,
                     bs, T.blurLOD, glassA);

        // Label
        float textScale = 0.55f * widthFactor * scale;
        float tw = ui.GetTextWidth(labels[i], textScale);
        float tx = bx + offsetX + (scaledW - tw) * 0.5f;
        float ty = barY + offsetY + liftY + (scaledH - ui.GetLineHeight(textScale)) * 0.5f;
        float bright = focused ? 1.0f : (0.7f + h * 0.3f);
        ui.DrawText(labels[i], tx, ty,
                    {accents[i].x * bright, accents[i].y * bright, accents[i].z * bright, 0.85f},
                    textScale);
    }
}
// ═════════════════════════════════════════════════════════════════════════════
// Background notes
// ═════════════════════════════════════════════════════════════════════════════

void MainMenuState::SpawnBackgroundNote(Context& ctx) {
    int vw = ctx.window->Width();
    std::uniform_real_distribution<float> distX(0, (float)vw);
    std::uniform_real_distribution<float> distSpeed(35.0f, 100.0f);
    std::uniform_real_distribution<float> distW(8.0f, 18.0f);
    std::uniform_real_distribution<float> distH(18.0f, 55.0f);
    std::uniform_real_distribution<float> distAlpha(0.3f, 0.7f);

    FallingNote n;
    n.x = distX(m_rng);
    n.y = -80.0f;
    n.speed = distSpeed(m_rng);
    n.width = distW(m_rng);
    n.height = distH(m_rng);
    n.color = util::ChannelColor(std::uniform_int_distribution<int>(0, 15)(m_rng));
    n.alpha = distAlpha(m_rng);

    // Occasional wider "chord" notes
    if (std::uniform_real_distribution<float>(0, 1)(m_rng) < 0.15f) {
        n.width *= 2.5f;
        n.height *= 1.5f;
        n.alpha *= 0.7f;
    }

    m_bgNotes.push_back(n);
    if (m_bgNotes.size() > 60) m_bgNotes.erase(m_bgNotes.begin());
}

// ═════════════════════════════════════════════════════════════════════════════
// Project Management
// ═════════════════════════════════════════════════════════════════════════════

void MainMenuState::LoadRecentProjects() {
    m_recentProjects.clear();
    std::ifstream in("recent_projects.txt");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            RecentProject p;
            p.filePath = line;
            std::filesystem::path path(line);
            p.name = path.stem().string();
            p.date = "Recently Opened";
            GenerateNotesFromMidi(p, line);
            m_recentProjects.push_back(std::move(p));
            if (m_recentProjects.size() >= 3) break;
        }
    }
}

void MainMenuState::SaveRecentProjects() {
    std::ofstream out("recent_projects.txt");
    for (const auto& p : m_recentProjects) {
        out << p.filePath << "\n";
    }
}

void MainMenuState::AddRecentProject(const std::string& path) {
    // Remove if already exists
    m_recentProjects.erase(
        std::remove_if(m_recentProjects.begin(), m_recentProjects.end(),
            [&](const RecentProject& p) { return p.filePath == path; }),
        m_recentProjects.end());
    
    // Insert at front
    RecentProject p;
    p.filePath = path;
    p.name = std::filesystem::path(path).stem().string();
    p.date = "Just Now";
    GenerateNotesFromMidi(p, path);
    m_recentProjects.insert(m_recentProjects.begin(), std::move(p));
    
    // Keep max 3
    if (m_recentProjects.size() > 3) {
        m_recentProjects.resize(3);
    }
}

void MainMenuState::GenerateNotesFromMidi(RecentProject& proj, const std::string& path) {
    proj.notes.clear();
    
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return;
    
    std::wstring wpath(wlen - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);

    MidiParser parser;
    if (!parser.Load(wpath)) {
        return; // Failed to load
    }

    const auto& notes = parser.Notes();
    if (notes.empty()) return;

    double duration = parser.Duration();
    if (duration <= 0.0) duration = 1.0;

    int minPitch = 128;
    int maxPitch = -1;
    for (const auto& n : notes) {
        if (n.note < minPitch) minPitch = n.note;
        if (n.note > maxPitch) maxPitch = n.note;
    }
    
    // Add some padding
    minPitch = std::max(0, minPitch - 2);
    maxPitch = std::min(127, maxPitch + 2);
    float pitchRange = (float)(maxPitch - minPitch);
    if (pitchRange < 1.0f) pitchRange = 1.0f;

    // Limit to 1000 notes for preview performance
    size_t count = std::min((size_t)1000, notes.size());
    size_t step = std::max((size_t)1, notes.size() / count);

    proj.notes.reserve(count);
    for (size_t i = 0; i < notes.size(); i += step) {
        const auto& n = notes[i];
        RecentProject::NoteBlock nb;
        nb.x = (float)(n.start / duration);
        nb.w = (float)((n.end - n.start) / duration);
        nb.y = 1.0f - (float)(n.note - minPitch) / pitchRange; // Invert Y
        nb.h = 1.0f / pitchRange;
        nb.color = util::ChannelColor(n.channel % 16);
        proj.notes.push_back(nb);
        if (proj.notes.size() >= count) break;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Layout (recalculated on resize)
// ═════════════════════════════════════════════════════════════════════════════

void MainMenuState::RebuildLayout(int vw, int vh) {
    m_lastW = vw;
    m_lastH = vh;

    // Main buttons (left side) - responsive positioning
    float mainBtnW = (float)vw * 0.28f;
    float mainBtnH = 65.0f;
    float mainBtnGap = 18.0f;
    float mainBtnX = (float)vw * 0.06f;
    
    // Vertically center main buttons based on available space
    float totalMainBtnHeight = 2 * mainBtnH + mainBtnGap;
    float mainBtnStartY = ((float)vh - totalMainBtnHeight) * 0.45f; // Slightly above center

    m_buttons[0] = {mainBtnX, mainBtnStartY, mainBtnW, mainBtnH, ButtonID::FreePlay};
    m_buttons[1] = {mainBtnX, mainBtnStartY + mainBtnH + mainBtnGap, mainBtnW, mainBtnH, ButtonID::OpenMidi};

    // Bottom bar buttons (right side) - dynamic positioning
    float baseBtnW = 110.0f;
    float baseBtnH = 40.0f;
    float btnGap = 10.0f;
    
    // Adjust button size based on viewport width
    float widthFactor = std::min(1.0f, std::max(0.8f, (float)vw / 1920.0f));
    float btnW = baseBtnW * widthFactor;
    float btnH = baseBtnH * widthFactor;
    
    // Dynamic positioning - ensure buttons fit within viewport
    float totalW = 3 * btnW + 2 * btnGap;
    float startX;
    
    // If window is too narrow, reduce button size to fit
    if (totalW > vw * 0.6f) {
        btnW = (vw * 0.6f - 2 * btnGap) / 3;
        btnH = btnW * 0.36f; // Maintain aspect ratio
        totalW = 3 * btnW + 2 * btnGap;
        startX = (float)vw - totalW - 20.0f;
    } else {
        startX = (float)vw - totalW - 20.0f;
    }
    
    float barY = (float)vh - 65.0f;

    m_buttons[2] = {startX, barY, btnW, btnH, ButtonID::SoundFont};
    m_buttons[3] = {startX + btnW + btnGap, barY, btnW, btnH, ButtonID::MidiDevice};
    m_buttons[4] = {startX + 2 * (btnW + btnGap), barY, btnW, btnH, ButtonID::Settings};

    // Project cards (used for hit testing) - responsive sizing
    float cardX = (float)vw * 0.52f;
    float cardW = (float)vw * 0.42f;
    float heightFactor = std::min(1.0f, std::max(0.7f, (float)vh / 1080.0f));
    float cardH = 170.0f * heightFactor;
    float cardGap = 14.0f;
    float cardY = 55.0f + 45.0f * heightFactor;

    // Update hit-test boxes for all project cards (up to max visible)
    for (int i = 0; i < std::min((int)m_recentProjects.size(), m_maxVisibleProjects); i++) {
        ButtonID pid = (ButtonID)((int)ButtonID::Project0 + i);
        float cy = cardY + i * (cardH + cardGap) - m_projectScrollOffset;
        m_buttons[(int)pid] = {cardX, cy, cardW, cardH, pid};
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Hit testing
// ═════════════════════════════════════════════════════════════════════════════

MainMenuState::ButtonID MainMenuState::HitTest(int mx, int my) {
    for (int i = 0; i < (int)ButtonID::Count; i++) {
        auto& b = m_buttons[i];
        if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h)
            return (ButtonID)i;
    }
    return ButtonID::Count; // nothing hit
}

void MainMenuState::UpdateHoverStates(int mx, int my) {
    for (int i = 0; i < (int)ButtonID::Count; i++) {
        m_buttons[i].hovered = false;
    }
    
    // Reset project hover states
    for (auto& proj : m_recentProjects) {
        proj.hovered = false;
    }
    
    ButtonID hit = HitTest(mx, my);
    if (hit != ButtonID::Count) {
        m_buttons[(int)hit].hovered = true;
    }
    
    // Check if hovering over a project card
    float cardX = (float)(m_lastW * 0.52f);
    float cardY = 100.0f - m_projectScrollOffset;
    float cardW = (float)m_lastW * 0.42f;
    float heightFactor = std::min(1.0f, std::max(0.7f, (float)m_lastH / 1080.0f));
    float cardH = 170.0f * heightFactor;
    float cardGap = 14.0f;
    
    for (int i = 0; i < (int)m_recentProjects.size(); i++) {
        float cy = cardY + i * (cardH + cardGap);
        if (mx >= cardX && mx <= cardX + cardW && 
            my >= cy && my <= cy + cardH) {
            m_recentProjects[i].hovered = true;
            break;
        }
    }
}

Transition MainMenuState::ActivateButton(Context& ctx, ButtonID id) {
    switch (id) {
    case ButtonID::FreePlay:
        return {StateID::FreePlay, true, true};

    case ButtonID::OpenMidi: {
        auto path = OpenMidiFileDialog(ctx.window->Handle());
        if (!path.empty()) {
            if (ctx.midi->Load(path)) {
                ctx.midiLoaded = true;
                int nlen = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string narrow(nlen - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), nlen, nullptr, nullptr);
                ctx.midiFilePath = narrow;
                
                AddRecentProject(narrow);
                SaveRecentProjects();
                
                return {StateID::MidiPlayback, true, true};
            }
        }
        return Transition::Handled();
    }

    case ButtonID::SoundFont:
        OpenSoundFontDialog(ctx);
        return Transition::Handled();

    case ButtonID::MidiDevice: {
        int count = MidiInput::DeviceCount();
        if (count == 0) {
            m_midiDeviceIndex = -1;
            ctx.midiInput->Close();
        } else {
            m_midiDeviceIndex = (m_midiDeviceIndex + 2) % (count + 1) - 1;
            if (m_midiDeviceIndex < 0) ctx.midiInput->Close();
            else ctx.midiInput->Open(m_midiDeviceIndex);
        }
        return Transition::Handled();
    }

    case ButtonID::Settings:
        // TODO: open settings panel
        return Transition::Handled();

    case ButtonID::Project0:
    case ButtonID::Project1:
    case ButtonID::Project2: {
        int idx = (int)id - (int)ButtonID::Project0;
        if (idx < (int)m_recentProjects.size()) {
            std::string path = m_recentProjects[idx].filePath; // Copy to avoid invalidation
            int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
            std::wstring wpath(wlen - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
            
            if (ctx.midi->Load(wpath)) {
                ctx.midiLoaded = true;
                ctx.midiFilePath = path;
                AddRecentProject(path); // Move to top
                SaveRecentProjects();
                return {StateID::MidiPlayback, true, true};
            }
        }
        return Transition::Handled();
    }

    default:
        break;
    }
    return Transition::Handled();
}

// ═════════════════════════════════════════════════════════════════════════════
// Input
// ═════════════════════════════════════════════════════════════════════════════

Transition MainMenuState::OnKey(Context& ctx, int key, bool down) {
    if (!down) return {};

    // Tab through focusable buttons
    if (key == VK_TAB) {
        // Focusable: FreePlay, OpenMidi, SoundFont, MidiDevice, Settings
        static const ButtonID order[] = {
            ButtonID::FreePlay, ButtonID::OpenMidi,
            ButtonID::SoundFont, ButtonID::MidiDevice, ButtonID::Settings
        };
        int cur = 0;
        for (int i = 0; i < 5; i++) {
            if (order[i] == m_focused) { cur = i; break; }
        }
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        cur = (cur + (shift ? 4 : 1)) % 5;
        m_focused = order[cur];
        return Transition::Handled();
    }

    if (key == VK_UP) {
        if (m_focused == ButtonID::OpenMidi) m_focused = ButtonID::FreePlay;
        else if (m_focused == ButtonID::MidiDevice) m_focused = ButtonID::SoundFont;
        else if (m_focused == ButtonID::Settings) m_focused = ButtonID::MidiDevice;
        return Transition::Handled();
    }
    if (key == VK_DOWN) {
        if (m_focused == ButtonID::FreePlay) m_focused = ButtonID::OpenMidi;
        else if (m_focused == ButtonID::SoundFont) m_focused = ButtonID::MidiDevice;
        else if (m_focused == ButtonID::MidiDevice) m_focused = ButtonID::Settings;
        return Transition::Handled();
    }
    if (key == VK_LEFT) {
        if (m_focused == ButtonID::SoundFont || m_focused == ButtonID::MidiDevice ||
            m_focused == ButtonID::Settings)
            m_focused = ButtonID::FreePlay;
        return Transition::Handled();
    }
    if (key == VK_RIGHT) {
        if (m_focused == ButtonID::FreePlay || m_focused == ButtonID::OpenMidi)
            m_focused = ButtonID::SoundFont;
        return Transition::Handled();
    }

    if (key == VK_RETURN || key == VK_SPACE) {
        return ActivateButton(ctx, m_focused);
    }

    if (key == VK_ESCAPE) return Transition::Handled();

    return {};
}

Transition MainMenuState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    UpdateHoverStates(x, y);

    if (move) {
        ButtonID hit = HitTest(x, y);
        if (hit != ButtonID::Count) m_focused = hit;
    }

    if (down) {
        ButtonID hit = HitTest(x, y);
        if (hit != ButtonID::Count) {
            // Trigger press animation
            m_buttons[(int)hit].pressAnim = 1.0f;
            return ActivateButton(ctx, hit);
        }
    }

    return {};
}

Transition MainMenuState::OnMouseWheel(Context& ctx, int delta) {
    // Scroll recent projects list
    float scrollSpeed = 30.0f;
    
    // Calculate max scroll based on number of projects and visible area
    float heightFactor = std::min(1.0f, std::max(0.7f, (float)m_lastH / 1080.0f));
    float cardH = 170.0f * heightFactor;
    float cardGap = 14.0f;
    float totalHeight = (float)m_recentProjects.size() * (cardH + cardGap);
    float availableHeight = (float)m_lastH - 200.0f;
    float maxScroll = std::max(0.0f, totalHeight - availableHeight);
    
    m_projectScrollOffset -= (float)delta * scrollSpeed / WHEEL_DELTA;
    m_projectScrollOffset = std::clamp(m_projectScrollOffset, 0.0f, maxScroll);
    
    return Transition::Handled();
}

void MainMenuState::HandleRightClick(int mx, int my) {
    // Check if clicked on a project card
    float cardX = (float)(m_lastW * 0.52f);
    float cardY = 100.0f - m_projectScrollOffset;
    float cardW = (float)m_lastW * 0.42f;
    float heightFactor = std::min(1.0f, std::max(0.7f, (float)m_lastH / 1080.0f));
    float cardH = 170.0f * heightFactor;
    float cardGap = 14.0f;
    
    for (int i = 0; i < (int)m_recentProjects.size(); i++) {
        float cy = cardY + i * (cardH + cardGap);
        if (mx >= cardX && mx <= cardX + cardW && 
            my >= cy && my <= cy + cardH) {
            // Show context menu for this project
            ShowContextMenu(nullptr, mx, my, i);
            break;
        }
    }
}

void MainMenuState::ShowContextMenu(HWND hWnd, int x, int y, int projectIndex) {
    // Create popup menu
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, 1, L"Open Project");
    AppendMenu(hMenu, MF_STRING, 2, L"Remove from List");
    AppendMenu(hMenu, MF_STRING, 3, L"Show in Folder");
    
    // Track popup menu
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, x, y, 0, hWnd, nullptr);
    
    DestroyMenu(hMenu);
    
    // Handle command
    switch (cmd) {
        case 1: { // Open Project
            if (projectIndex >= 0 && projectIndex < (int)m_recentProjects.size()) {
                // This would need access to Context to load the project
                // For now, just a placeholder
            }
            break;
        }
        case 2: { // Remove from List
            if (projectIndex >= 0 && projectIndex < (int)m_recentProjects.size()) {
                m_recentProjects.erase(m_recentProjects.begin() + projectIndex);
                SaveRecentProjects();
            }
            break;
        }
        case 3: { // Show in Folder
            if (projectIndex >= 0 && projectIndex < (int)m_recentProjects.size()) {
                const auto& path = m_recentProjects[projectIndex].filePath;
                // Open explorer to file location
                std::wstring wpath(path.begin(), path.end());
                ShellExecuteW(nullptr, L"open", L"explorer.exe", 
                             (L"/select," + wpath).c_str(), nullptr, SW_SHOW);
            }
            break;
        }
    }
}

void MainMenuState::DrawHint(Context& ctx) {
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    const char* hint = "Arrow keys / mouse to navigate  |  Enter to select";
    float hw = ui.GetTextWidth(hint, 0.6f);
    ui.DrawText(hint, ((float)vw - hw) * 0.5f, (float)vh - 20.0f, T.textMuted, 0.6f);
}

} // namespace pfd
