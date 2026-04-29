#include "MidiPlaybackState.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <Windows.h>
#include <commdlg.h>

namespace pfd {

void MidiPlaybackState::Enter(Context& ctx) {
    m_playbackTime = 0;
    m_playbackTick = 0;
    m_nextEventIdx = 0;
    m_playing = false;
    m_loop = true;
    m_pause.Close();
    m_fpsAccum = 0;
    m_fpsCount = 0;
    m_fpsDisplay = 0;
    m_draggingTimeline = false;
    m_showUI = true;
    m_playbackSpeed = 1.0;

    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
    ctx.input->ClearEvents();

    if (ctx.midiLoaded) {
        m_sortedEvents = ctx.midi->GetAllEventsSorted();
        m_playing = true;
    }
}

void MidiPlaybackState::Exit(Context& ctx) {
    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
}

void MidiPlaybackState::LoadMidiFile(Context& ctx, const std::wstring& path) {
    if (ctx.midi->Load(path)) {
        ctx.midiLoaded = true;
        m_sortedEvents = ctx.midi->GetAllEventsSorted();
        m_nextEventIdx = 0;
        m_playbackTime = 0;
        m_playbackTick = 0;
        m_playing = true;

        int nlen = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string narrow(nlen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), nlen, nullptr, nullptr);
        ctx.midiFilePath = narrow;

        ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
        ctx.audio->AllNotesOff();
    }
}

Transition MidiPlaybackState::Update(Context& ctx, double dt) {
    if (m_pause.IsOpen()) {
        m_pause.Update(dt);
        ctx.audio->ProcessEvents();
        return {};
    }

    m_fpsAccum += (float)dt;
    m_fpsCount++;
    if (m_fpsAccum >= 1.0f) {
        m_fpsDisplay = m_fpsCount;
        m_fpsCount = 0;
        m_fpsAccum -= 1.0f;
    }

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

    if (ctx.midiLoaded && m_playing) {
        m_playbackTime += dt * m_playbackSpeed;
        m_playbackTick = ctx.midi->SecondsToTicks(m_playbackTime);

        if (m_playbackTime >= ctx.midi->Duration()) {
            if (m_loop) {
                SeekTo(ctx, 0);
            } else {
                m_playing = false;
                m_playbackTime = ctx.midi->Duration();
            }
        }

        while (m_nextEventIdx < m_sortedEvents.size() &&
               m_sortedEvents[m_nextEventIdx].tick <= m_playbackTick) {
            auto& ev = m_sortedEvents[m_nextEventIdx];
            uint8_t type = ev.status & 0xF0;
            uint8_t chan = ev.status & 0x0F;

            if (ev.isNoteOn) {
                ctx.noteState->NoteOn(ev.data1, ev.data2, chan, m_playbackTime);
                ctx.audio->NoteOn(chan, ev.data1, ev.data2);
            } else if (ev.isNoteOff) {
                ctx.noteState->NoteOff(ev.data1, chan, m_playbackTime);
                ctx.audio->NoteOff(chan, ev.data1);
            } else if (type == 0xB0) {
                ctx.audio->ControlChange(chan, ev.data1, ev.data2);
            } else if (type == 0xE0) {
                ctx.audio->PitchBend(chan, ev.data1 | (ev.data2 << 7));
            } else if (type == 0xC0) {
                ctx.audio->ProgramChange(chan, ev.data1);
            } else if (type == 0xD0) {
                ctx.audio->ChannelPressure(chan, ev.data1);
            } else if (type == 0xA0) {
                ctx.audio->KeyPressure(chan, ev.data1, ev.data2);
            }
            m_nextEventIdx++;
        }
    }

    ctx.audio->ProcessEvents();
    ctx.piano->Update(*ctx.noteState, (float)m_playbackTime, (float)dt);
    ctx.noteState->ClearRecentEvents();

    return {};
}

void MidiPlaybackState::Render(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    batch.Begin(ctx.d3d->Context(), vw, vh);

    batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, 1.0f});

    if (ctx.midiLoaded) {
        ctx.piano->Render(batch, *ctx.noteState, ctx.midi->Notes(),
                          (float)m_playbackTime, (float)m_playbackTime, (float)ctx.timer->Delta());
        if (m_showUI) {
            DrawTimeline(ctx);
            DrawControls(ctx);
        }
    } else {
        DrawLoadPrompt(ctx);
    }

    // FPS (only when UI visible)
    if (m_showUI) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "FPS: %d", m_fpsDisplay);
        ctx.font->DrawText(batch, buf, 10, 10, 0.4f, 0.4f, 0.5f, 0.55f);
    }

    // Pause menu on top
    m_pause.Render(batch, *ctx.font, ctx.piano->GetNoteTex(), vw, vh);

    batch.End();
}

void MidiPlaybackState::DrawTimeline(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    auto& font = *ctx.font;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    float tlX = 120.0f;
    float tlW = (float)vw - 240.0f;
    float tlY = (float)vh - ctx.piano->GetPianoHeight() - 35.0f;
    float tlH = 8.0f;

    float progress = (ctx.midi->Duration() > 0)
        ? (float)(m_playbackTime / ctx.midi->Duration()) : 0;
    progress = std::clamp(progress, 0.0f, 1.0f);

    batch.Draw({tlX, tlY}, {tlW, tlH}, {0.1f, 0.1f, 0.15f, 0.8f});
    batch.Draw({tlX, tlY}, {tlW * progress, tlH}, {0.3f, 0.6f, 1.0f, 0.9f});

    batch.SetBlendMode(true);
    batch.Draw({tlX + tlW * progress - 5, tlY - 3}, {10, tlH + 6}, {0.5f, 0.8f, 1.0f, 0.8f});
    batch.SetBlendMode(false);

    auto formatTime = [](double secs) -> std::string {
        int m = (int)(secs / 60), s = (int)secs % 60;
        char buf[16]; std::snprintf(buf, sizeof(buf), "%d:%02d", m, s); return buf;
    };
    font.DrawText(batch, formatTime(m_playbackTime), 10, tlY - 5, 0.5f, 0.5f, 0.6f, 0.55f);
    font.DrawText(batch, formatTime(ctx.midi->Duration()), vw - 60.0f, tlY - 5, 0.5f, 0.5f, 0.6f, 0.55f);

    if (!ctx.midiFilePath.empty()) {
        std::filesystem::path p(ctx.midiFilePath);
        font.DrawText(batch, p.filename().string(), 10, 30, 0.4f, 0.5f, 0.6f, 0.55f);
    }
}

void MidiPlaybackState::DrawControls(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();
    float cy = (float)vh - ctx.piano->GetPianoHeight() - 65.0f;
    float cx = (float)vw * 0.5f;

    const char* playLabel = m_playing ? "PAUSE" : "PLAY";
    float pw = font.GetTextWidth(playLabel, 0.7f);
    font.DrawText(batch, playLabel, cx - pw * 0.5f, cy, 0.4f, 0.8f, 0.4f, 0.7f);

    const char* loopLabel = m_loop ? "LOOP: ON" : "LOOP: OFF";
    font.DrawText(batch, loopLabel, vw - 120.0f, cy,
                  m_loop ? 0.3f : 0.5f, m_loop ? 0.7f : 0.4f, m_loop ? 0.4f : 0.4f, 0.55f);

    const char* hint = "Space: Play/Pause  |  L: Loop  |  H: Hide UI  |  Left/Right: Seek  |  ESC: Menu";
    float hw = font.GetTextWidth(hint, 0.45f);
    font.DrawText(batch, hint, (vw - hw) * 0.5f, cy + 20, 0.25f, 0.25f, 0.35f, 0.45f);
}

void MidiPlaybackState::DrawLoadPrompt(Context& ctx) {
    auto& font = *ctx.font;
    auto& batch = *ctx.spriteBatch;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    const char* msg = "No MIDI file loaded";
    float mw = font.GetTextWidth(msg, 1.0f);
    font.DrawText(batch, msg, (vw - mw) * 0.5f, vh * 0.4f, 0.5f, 0.5f, 0.6f, 1.0f);

    const char* hint = "Press O to open a MIDI file  |  ESC: Menu";
    float hw = font.GetTextWidth(hint, 0.7f);
    font.DrawText(batch, hint, (vw - hw) * 0.5f, vh * 0.4f + 35, 0.3f, 0.5f, 0.8f, 0.7f);
}

Transition MidiPlaybackState::OnKey(Context& ctx, int key, bool down) {
    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnKey(key, down);
        if (action == PauseAction::Resume) return {};
        if (action == PauseAction::ToggleSpeed) {
            static const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
            int current = 2; // Default 1.0
            for(int i=0; i<6; i++) if(std::abs(m_playbackSpeed - speeds[i]) < 0.01) { current = i; break; }
            m_playbackSpeed = speeds[(current + 1) % 6];
            char buf[32]; std::snprintf(buf, sizeof(buf), "SPEED: %.2fx", m_playbackSpeed);
            m_pause.SetLabel(1, buf);
            return Transition::Handled();
        }
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
        char sbuf[32]; std::snprintf(sbuf, sizeof(sbuf), "SPEED: %.2fx", m_playbackSpeed);
        m_pause.SetLabel(1, sbuf);
        m_pause.SetLabel(3, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
        m_pause.Open();
        ctx.audio->AllNotesOff();
        return Transition::Handled();
    }
    if (key == 'O') {
        wchar_t filePath[MAX_PATH] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = ctx.window->Handle();
        ofn.lpstrFilter = L"MIDI Files (*.mid;*.midi)\0*.mid;*.midi\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Open MIDI File";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) LoadMidiFile(ctx, filePath);
        return Transition::Handled();
    }
    if (key == VK_SPACE) { m_playing = !m_playing; return Transition::Handled(); }
    if (key == 'L') { m_loop = !m_loop; return Transition::Handled(); }
    if (key == 'H') { m_showUI = !m_showUI; return Transition::Handled(); }
    if (key == VK_RIGHT && ctx.midiLoaded) {
        SeekTo(ctx, m_playbackTime + 5.0);
        return Transition::Handled();
    }
    if (key == VK_LEFT && ctx.midiLoaded) {
        SeekTo(ctx, m_playbackTime - 5.0);
        return Transition::Handled();
    }
    if (key == 'R' && ctx.midiLoaded) {
        SeekTo(ctx, 0);
        m_playing = true;
        return Transition::Handled();
    }

    return {};
}

Transition MidiPlaybackState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnMouse(x, y, down, move, vw, vh);
        if (action == PauseAction::Resume) return {};
        if (action == PauseAction::ToggleSpeed) {
            static const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
            int current = 2;
            for(int i=0; i<6; i++) if(std::abs(m_playbackSpeed - speeds[i]) < 0.01) { current = i; break; }
            m_playbackSpeed = speeds[(current + 1) % 6];
            char buf[32]; std::snprintf(buf, sizeof(buf), "SPEED: %.2fx", m_playbackSpeed);
            m_pause.SetLabel(1, buf);
            return Transition::Handled();
        }
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

    // Timeline interaction
    if (ctx.midiLoaded) {
        float tlX = 120.0f, tlW = (float)vw - 240.0f;
        float tlY = (float)vh - ctx.piano->GetPianoHeight() - 35.0f;
        float tlH = 8.0f;
        bool onTimeline = (x >= tlX && x <= tlX + tlW && y >= tlY - 10 && y <= tlY + tlH + 10);

        if (down && onTimeline) m_draggingTimeline = true;
        if (m_draggingTimeline) {
            if (down) {
                float progress = std::clamp((x - tlX) / tlW, 0.0f, 1.0f);
                SeekTo(ctx, progress * ctx.midi->Duration());
            } else {
                m_draggingTimeline = false;
                m_showUI = true;
            }
        }
    }

    return {};
}

void MidiPlaybackState::SeekTo(Context& ctx, double newTime) {
    if (!ctx.midiLoaded) return;
    m_playbackTime = std::clamp(newTime, 0.0, ctx.midi->Duration());
    m_playbackTick = ctx.midi->SecondsToTicks(m_playbackTime);

    // Clear all active notes
    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
    ctx.noteState->ClearVisualNotes();

    // Fast-forward nextEventIdx and catch up audio state
    m_nextEventIdx = 0;
    while (m_nextEventIdx < m_sortedEvents.size() && m_sortedEvents[m_nextEventIdx].tick <= m_playbackTick) {
        auto& ev = m_sortedEvents[m_nextEventIdx];
        uint8_t type = ev.status & 0xF0;
        uint8_t chan = ev.status & 0x0F;

        // Catch up state changes like Program Change and Control Change (Sustain, etc.)
        if (type == 0xB0) ctx.audio->ControlChange(chan, ev.data1, ev.data2);
        else if (type == 0xC0) ctx.audio->ProgramChange(chan, ev.data1);
        else if (type == 0xE0) ctx.audio->PitchBend(chan, ev.data1 | (ev.data2 << 7));

        m_nextEventIdx++;
    }
}

} // namespace pfd
