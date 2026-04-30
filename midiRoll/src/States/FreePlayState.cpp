#include "FreePlayState.h"
#include "StateHelpers.h"
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
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();
    float currentTime = (float)ctx.timer->Elapsed();

    batch.Begin(ctx.d3d->Context(), vw, vh);

    if (m_backgroundTex) {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, m_backgroundTex.Get(), {0,0}, {1,1}, {1,1,1,1});
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0, 0, 0, 0.35f});
    } else {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, 1.0f});
    }

    ctx.piano->Render(batch, *ctx.noteState, {}, currentTime, currentTime, ctx.deltaTime, ctx.d3d->Context());

    if (m_showHUD) DrawHUD(ctx);

    // Capture screen for blur before drawing pause menu overlay
    ID3D11ShaderResourceView* screenTex = nullptr;
    if (m_pause.IsOpen()) {
        batch.End();
        screenTex = ctx.d3d->CaptureScreen();
        batch.Begin(ctx.d3d->Context(), vw, vh);
    }

    // Pause menu on top
    m_pause.Render(batch, *ctx.font, ctx.piano->GetNoteTex(), vw, vh, screenTex);

    batch.End();
}

void FreePlayState::DrawHUD(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;

    font.DrawText(batch, "FREE PLAY", 12, 12, 0.35f, 0.85f, 1.0f, 1.0f);

    int activeCount = 0;
    for (int i = 0; i < 128; i++) {
        if ((*ctx.noteState)[i].active) activeCount++;
    }
    if (activeCount > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Notes: %d", activeCount);
        font.DrawText(batch, buf, 12, 42, 0.7f, 0.7f, 0.8f, 0.75f);
    }

    if (ctx.audio->IsSoundFontLoaded()) {
        std::filesystem::path p(ctx.soundFontPath);
        font.DrawText(batch, "SF2: " + p.filename().string(), 12, 68, 0.4f, 0.75f, 0.4f, 0.75f);
    } else {
        font.DrawText(batch, "SF2: none", 12, 68, 0.6f, 0.4f, 0.3f, 0.75f);
    }

    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        font.DrawText(batch, "MIDI: " + ctx.midiInput->DeviceName(), 12, 94, 0.35f, 0.75f, 0.9f, 0.75f);
    } else {
        font.DrawText(batch, "MIDI: no device", 12, 94, 0.65f, 0.45f, 0.45f, 0.75f);
    }

    const char* hint = "ESC: Menu  |  F1: Toggle HUD";
    float hy = (float)ctx.window->Height() - ctx.piano->GetPianoHeight() - 30.0f;
    font.DrawText(batch, hint, 12, hy, 0.55f, 0.55f, 0.65f, 0.75f);
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
