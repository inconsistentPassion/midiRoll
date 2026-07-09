#include "FreePlayState.h"
#include "StateHelpers.h"
#include "../Renderer/FrostedGlassTheme.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <Windows.h>
#include <commdlg.h>

namespace pfd {

void FreePlayState::Enter(Context& ctx) {
    m_liveTime = 0;
    m_mouseNotes.fill(false);
    m_showHUD = true;
    m_pause.Close();
    ctx.input->ClearEvents();
    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
}

void FreePlayState::Exit(Context& ctx) {
    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
}

Transition FreePlayState::Update(Context& ctx, double dt) {
    if (m_pause.IsOpen()) {
        m_pause.Update(dt);
        ctx.audio->ProcessEvents();
        return {};
    }

    m_liveTime += dt;
    double currentTime = ctx.timer->Elapsed();

    // Process keyboard/mouse note events
    for (auto& ev : ctx.input->GetEvents()) {
        if (ev.isDown) {
            int vel = std::clamp(ev.velocity, 1, 127);
            ctx.noteState->NoteOn(ev.note, vel, 15, currentTime);
            ctx.audio->NoteOn(15, ev.note, vel);
        } else {
            ctx.noteState->NoteOff(ev.note, 15, currentTime);
            ctx.audio->NoteOff(15, ev.note);
        }
    }
    ctx.input->ClearEvents();

    // Process MIDI device control changes (sustain pedal, etc.)
    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        for (auto& ev : ctx.midiInput->Poll()) {
            using Kind = MidiInput::NoteEvent::Kind;
            switch (ev.kind) {
            case Kind::NoteOn:
                ctx.noteState->NoteOn(ev.data1, ev.data2, ev.channel, currentTime);
                ctx.audio->NoteOn(ev.channel, ev.data1, ev.data2);
                break;
            case Kind::NoteOff:
                ctx.noteState->NoteOff(ev.data1, ev.channel, currentTime);
                ctx.audio->NoteOff(ev.channel, ev.data1);
                break;
            case Kind::ControlChange:
                ctx.audio->ControlChange(ev.channel, ev.data1, ev.data2);
                // CC #64 = sustain pedal
                if (ev.data1 == 64) {
                    if (ev.data2 >= 64) ctx.noteState->SustainOn(ev.channel);
                    else                ctx.noteState->SustainOff(ev.channel, currentTime);
                }
                break;
            case Kind::PitchBend:
                ctx.audio->PitchBend(ev.channel, ev.data2);
                break;
            case Kind::ProgramChange:
                ctx.audio->ProgramChange(ev.channel, ev.data1);
                break;
            case Kind::ChannelPressure:
                ctx.audio->ChannelPressure(ev.channel, ev.data1);
                break;
            case Kind::KeyPressure:
                ctx.audio->KeyPressure(ev.channel, ev.data1, ev.data2);
                break;
            }
        }
    }

    ctx.audio->ProcessEvents();
    ctx.piano->Update(*ctx.noteState, (float)currentTime, (float)dt);
    ctx.noteState->ClearRecentEvents();

    return {};
}

void FreePlayState::Render(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    auto& ui = *ctx.ui;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();
    float currentTime = (float)ctx.timer->Elapsed();

    // ── Phase 1: Render game world via SpriteBatch ──
    batch.Begin(ctx.d3d->Context(), vw, vh);

    if (m_backgroundTex) {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, m_backgroundTex.Get(), {0,0}, {1,1}, {1,1,1,1});
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0, 0, 0, 0.35f});
    } else {
        auto& T = glass::GetTheme();
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {T.bgColor.x, T.bgColor.y, T.bgColor.z, 1.0f});
    }

    ctx.piano->Render(batch, *ctx.noteState, {}, currentTime, currentTime, ctx.deltaTime, ctx.d3d->Context());
    batch.End();

    // ── Phase 2: Capture scene for glass ──
    auto* sceneSRV = ctx.d3d->CaptureSceneForGlass();
    float maxLOD = ctx.d3d->GetSceneMaxLOD();

    // ── Phase 3: Background blobs ──
    ctx.glass->Render(ctx.d3d->Context(), vw, vh, (float)m_liveTime);

    // ── Phase 4: Glass UI overlay ──
    ui.Begin(ctx.d3d->Context(), vw, vh, sceneSRV, maxLOD);

    if (m_showHUD) DrawHUD(ctx);

    // Pause menu (glass version)
    if (m_pause.IsOpen()) {
        ui.FlushGlass();
        m_pause.RenderGlass(ui, vw, vh, sceneSRV, maxLOD);
    }

    ui.End();
}

void FreePlayState::DrawHUD(Context& ctx) {
    auto& ui = *ctx.ui;
    auto& T = glass::GetTheme();

    // HUD panel — glass material
    UIRenderer::RectStyle panelStyle;
    panelStyle.color = util::Vec4(0, 0, 0, 0);
    panelStyle.cornerRadius = T.radiusLg;
    panelStyle.borderWidth = 1.0f;
    panelStyle.borderColor = util::Vec4(1, 1, 1, T.glassBorderAlpha);
    ui.DrawGlass(8, 8, 260, 120, panelStyle, T.blurLODLight, T.glassAlpha);

    ui.DrawText("FREE PLAY", 20, 18, T.accentBlue, 1.0f);

    int activeCount = 0;
    for (int i = 0; i < 128; i++) {
        if ((*ctx.noteState)[i].active) activeCount++;
    }
    if (activeCount > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Notes: %d", activeCount);
        ui.DrawText(buf, 20, 48, T.textSecondary, 0.75f);
    }

    if (ctx.audio->IsSoundFontLoaded()) {
        std::filesystem::path p(ctx.soundFontPath);
        ui.DrawText("SF2: " + p.filename().string(), 20, 74, T.accentGreen, 0.75f);
    } else {
        ui.DrawText("SF2: none", 20, 74, T.accentRed, 0.75f);
    }

    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        ui.DrawText("MIDI: " + ctx.midiInput->DeviceName(), 20, 100, T.accentBlue, 0.75f);
    } else {
        ui.DrawText("MIDI: no device", 20, 100, T.textMuted, 0.75f);
    }

    const char* hint = "ESC: Menu  |  F1: Toggle HUD";
    float hy = (float)ctx.window->Height() - ctx.piano->GetPianoHeight() - 30.0f;
    ui.DrawText(hint, 12, hy, T.textMuted, 0.75f);
}

Transition FreePlayState::OnKey(Context& ctx, int key, bool down) {
    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnKey(key, down);
        if (action == PauseAction::Resume)           return {};
        if (action == PauseAction::ChangeSoundFont)  { OpenSoundFontDialog(ctx); return Transition::Handled(); }
        if (action == PauseAction::ChangeBackground) { OpenBackgroundDialog(ctx, m_backgroundTex); return Transition::Handled(); }
        if (action == PauseAction::ToggleMode) {
            ctx.piano->ToggleDirection();
            m_pause.SetLabel(4, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
            return Transition::Handled();
        }
        if (action == PauseAction::BackToMenu) return {StateID::MainMenu, true, true};
        if (action == PauseAction::Quit) { ctx.window->RequestClose(); return Transition::Handled(); }
        return Transition::Handled();
    }

    if (!down) return {};

    if (key == VK_ESCAPE) {
        m_pause.SetLabel(1, "SPEED: N/A");
        m_pause.SetLabel(4, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
        m_pause.Open();
        ctx.audio->AllNotesOff();
        return Transition::Handled();
    }
    if (key == VK_F1) {
        m_showHUD = !m_showHUD;
        return Transition::Handled();
    }

    return {};
}

Transition FreePlayState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnMouse(x, y, down, move, ctx.window->Width(), ctx.window->Height());
        if (action == PauseAction::Resume)           return {};
        if (action == PauseAction::ChangeSoundFont)  { OpenSoundFontDialog(ctx); return Transition::Handled(); }
        if (action == PauseAction::ChangeBackground) { OpenBackgroundDialog(ctx, m_backgroundTex); return Transition::Handled(); }
        if (action == PauseAction::ToggleMode) {
            ctx.piano->ToggleDirection();
            m_pause.SetLabel(4, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
            return Transition::Handled();
        }
        if (action == PauseAction::BackToMenu) return {StateID::MainMenu, true, true};
        if (action == PauseAction::Quit) { ctx.window->RequestClose(); return Transition::Handled(); }
        return Transition::Handled();
    }

    // Piano interaction
    double currentTime = ctx.timer->Elapsed();
    int currentKey = -1;
    if (y >= ctx.piano->GetPianoY() && y <= ctx.window->Height()) {
        for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
            float kx = ctx.piano->GetKeyX(n);
            float kw = ctx.piano->GetKeyWidth(n);
            if (x >= kx && x <= kx + kw) {
                currentKey = n;
                break;
            }
        }
    }

    if (down) {
        for (int n = 0; n < 128; n++) {
            if (m_mouseNotes[n] && n != currentKey) {
                ctx.noteState->NoteOff(n, 15, currentTime);
                ctx.audio->NoteOff(15, n);
                m_mouseNotes[n] = false;
            }
        }
        if (currentKey != -1 && !m_mouseNotes[currentKey]) {
            ctx.noteState->NoteOn(currentKey, 100, 15, currentTime);
            ctx.audio->NoteOn(15, currentKey, 100);
            m_mouseNotes[currentKey] = true;
        }
    } else {
        for (int n = 0; n < 128; n++) {
            if (m_mouseNotes[n]) {
                ctx.noteState->NoteOff(n, 15, currentTime);
                ctx.audio->NoteOff(15, n);
                m_mouseNotes[n] = false;
            }
        }
    }

    return {};
}

} // namespace pfd
