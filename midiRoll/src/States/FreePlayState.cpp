#include "FreePlayState.h"
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

    for (auto& ev : ctx.input->GetEvents()) {
        if (ev.isDown) {
            ctx.noteState->NoteOn(ev.note, 100, 0, currentTime);
            ctx.audio->NoteOn(0, ev.note, 100);
        } else {
            ctx.noteState->NoteOff(ev.note, 0, currentTime);
            ctx.audio->NoteOff(0, ev.note);
        }
    }
    ctx.input->ClearEvents();

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

    batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, 1.0f});

    ctx.piano->Render(batch, *ctx.noteState, {}, currentTime, currentTime, (float)ctx.timer->Delta());

    if (m_showHUD) DrawHUD(ctx);

    // Pause menu on top
    m_pause.Render(batch, *ctx.font, ctx.piano->GetNoteTex(), vw, vh);

    batch.End();
}

void FreePlayState::DrawHUD(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;

    font.DrawText(batch, "FREE PLAY", 10, 10, 0.3f, 0.8f, 1.0f, 0.8f);

    int activeCount = 0;
    for (int i = 0; i < 128; i++) {
        if ((*ctx.noteState)[i].active) activeCount++;
    }
    if (activeCount > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Notes: %d", activeCount);
        font.DrawText(batch, buf, 10, 35, 0.5f, 0.5f, 0.6f, 0.6f);
    }

    if (ctx.audio->IsSoundFontLoaded()) {
        std::filesystem::path p(ctx.soundFontPath);
        font.DrawText(batch, "SF2: " + p.filename().string(), 10, 55, 0.3f, 0.5f, 0.3f, 0.5f);
    }

    const char* hint = "ESC: Menu  |  F1: Toggle HUD";
    font.DrawText(batch, hint, 10, (float)ctx.window->Height() - ctx.piano->GetPianoHeight() - 25,
                  0.25f, 0.25f, 0.35f, 0.45f);
}

Transition FreePlayState::OnKey(Context& ctx, int key, bool down) {
    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnKey(key, down);
        if (action == PauseAction::Resume) return {};
        if (action == PauseAction::ChangeSoundFont) {
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
        if (action == PauseAction::ToggleMode) {
            ctx.piano->ToggleDirection();
            m_pause.SetLabel(3, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
            return Transition::Handled();
        }
        if (action == PauseAction::BackToMenu) return {StateID::MainMenu, true, true};
        if (action == PauseAction::Quit) { ctx.window->RequestClose(); return Transition::Handled(); }
        return Transition::Handled();
    }

    if (!down) return {};

    if (key == VK_ESCAPE) {
        m_pause.SetLabel(1, "SPEED: N/A");
        m_pause.SetLabel(3, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
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
        if (action == PauseAction::Resume) return {};
        if (action == PauseAction::ChangeSoundFont) {
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
        if (action == PauseAction::ToggleMode) {
            ctx.piano->ToggleDirection();
            m_pause.SetLabel(3, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
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
                ctx.noteState->NoteOff(n, 0, currentTime);
                ctx.audio->NoteOff(0, n);
                m_mouseNotes[n] = false;
            }
        }
        if (currentKey != -1 && !m_mouseNotes[currentKey]) {
            ctx.noteState->NoteOn(currentKey, 100, 0, currentTime);
            ctx.audio->NoteOn(0, currentKey, 100);
            m_mouseNotes[currentKey] = true;
        }
    } else {
        for (int n = 0; n < 128; n++) {
            if (m_mouseNotes[n]) {
                ctx.noteState->NoteOff(n, 0, currentTime);
                ctx.audio->NoteOff(0, n);
                m_mouseNotes[n] = false;
            }
        }
    }

    return {};
}

} // namespace pfd
