#include "MidiPlaybackState.h"
#include "StateHelpers.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <Windows.h>
#include <commdlg.h>
#include "../Util/TextureLoader.h"

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
    m_clockAnchor = Clock::now();
    m_timeAtAnchor = 0;

    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
    ctx.noteState->ClearVisualNotes();
    ctx.input->ClearEvents();

    if (ctx.midiLoaded) {
        m_sortedEvents = ctx.midi->GetAllEventsSorted();
        m_playing = true;
        m_clockAnchor = Clock::now();
        m_timeAtAnchor = -3.0; // 3-second lead-in before first note
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
        m_clockAnchor  = Clock::now();
        m_timeAtAnchor = -3.0; // 3-second lead-in before first note

        ctx.audio->AllNotesOff();
        ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
        ctx.noteState->ClearVisualNotes();

        int nlen = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string narrow(nlen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), nlen, nullptr, nullptr);
        ctx.midiFilePath = narrow;
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


    // Process keyboard input events
    for (auto& ev : ctx.input->GetEvents()) {
        if (ev.isDown) {
            int vel = std::clamp(ev.velocity, 1, 127);
            ctx.noteState->NoteOn(ev.note, vel, 15, m_playbackTime);
            ctx.audio->NoteOn(15, ev.note, vel);
        } else {
            ctx.noteState->NoteOff(ev.note, 15, m_playbackTime);
            ctx.audio->NoteOff(15, ev.note);
        }
    }
    ctx.input->ClearEvents();

    // Process live MIDI device input (for playing along with MIDI file)
    if (ctx.midiInput && ctx.midiInput->IsOpen()) {
        for (auto& ev : ctx.midiInput->Poll()) {
            using Kind = MidiInput::NoteEvent::Kind;
            switch (ev.kind) {
            case Kind::NoteOn:
                ctx.noteState->NoteOn(ev.data1, ev.data2, ev.channel, m_playbackTime);
                ctx.audio->NoteOn(ev.channel, ev.data1, ev.data2);
                break;
            case Kind::NoteOff:
                ctx.noteState->NoteOff(ev.data1, ev.channel, m_playbackTime);
                ctx.audio->NoteOff(ev.channel, ev.data1);
                break;
            case Kind::ControlChange:
                ctx.audio->ControlChange(ev.channel, ev.data1, ev.data2);
                // CC #64 = sustain pedal
                if (ev.data1 == 64) {
                    if (ev.data2 >= 64) ctx.noteState->SustainOn(ev.channel);
                    else                ctx.noteState->SustainOff(ev.channel, m_playbackTime);
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

    if (ctx.midiLoaded && m_playing) {
        // Derive playback time from wall clock — immune to frame-rate jitter.
        using Dur = std::chrono::duration<double>;
        double wallElapsed = Dur(Clock::now() - m_clockAnchor).count();
        m_playbackTime = m_timeAtAnchor + wallElapsed * m_playbackSpeed;
        // Only compute tick from non-negative playback time
        m_playbackTick = (m_playbackTime >= 0.0)
            ? ctx.midi->SecondsToTicks(m_playbackTime)
            : 0;

        // Use m_pianoY-based lookahead estimate as tail buffer so notes clear screen
        static constexpr double TAIL_BUFFER = 3.0;  // seconds after last note

        if (m_playbackTime >= ctx.midi->Duration() + TAIL_BUFFER) {
            if (m_loop) {
                SeekTo(ctx, -3.0); // lead-in on loop: notes scroll in before playing
            } else {
                m_playing = false;
                m_playbackTime = ctx.midi->Duration() + TAIL_BUFFER;
            }
        }

        // Compare against ev.time (seconds) directly — avoids SecondsToTicks rounding
        // which causes ±1 tick (~few ms) jitter per event.
        while (m_nextEventIdx < m_sortedEvents.size() &&
               m_sortedEvents[m_nextEventIdx].time <= m_playbackTime) {
            auto& ev = m_sortedEvents[m_nextEventIdx];
            uint8_t type = ev.status & 0xF0;
            uint8_t chan = ev.globalChannel;

            if (ev.isNoteOn) {
                ctx.noteState->NoteOn(ev.data1, ev.data2, chan, m_playbackTime);
                ctx.audio->NoteOn(chan, ev.data1, ev.data2);
            } else if (ev.isNoteOff) {
                ctx.noteState->NoteOff(ev.data1, chan, m_playbackTime);
                ctx.audio->NoteOff(chan, ev.data1);
            } else if (type == 0xB0) {
                ctx.audio->ControlChange(chan, ev.data1, ev.data2);
                // CC #64 = sustain pedal: track visually
                if (ev.data1 == 64) {
                    if (ev.data2 >= 64) ctx.noteState->SustainOn(chan);
                    else                ctx.noteState->SustainOff(chan, m_playbackTime);
                }
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

    if (m_backgroundTex) {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, m_backgroundTex.Get(), {0,0}, {1,1}, {1,1,1,1});
        // Apply dimming overlay
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0, 0, 0, m_backgroundDim});
    } else {
        batch.Draw({0, 0}, {(float)vw, (float)vh}, {0.02f, 0.02f, 0.04f, 1.0f});
    }

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

void MidiPlaybackState::DrawTimeline(Context& ctx) {
    auto& batch = *ctx.spriteBatch;
    auto& font = *ctx.font;
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    float tlX = 120.0f;
    float tlW = (float)vw - 240.0f;
    float tlY = (float)vh - ctx.piano->GetPianoHeight() - 35.0f;
    float tlH = 8.0f;

    float leadIn = 3.0f;
    float tailBuf = 3.0f;
    float totalDuration = (float)ctx.midi->Duration() + leadIn + tailBuf;
    
    float progress = (totalDuration > 0)
        ? (float)((m_playbackTime + leadIn) / totalDuration) : 0;
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
        if (action == PauseAction::Resume) {
            m_pause.Close();
            m_playing = true;
            m_clockAnchor = Clock::now();
            m_timeAtAnchor = m_playbackTime;
            return Transition::Handled();
        }
        if (action == PauseAction::ToggleSpeed) {
            static const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
            int current = 2;
            for(int i=0; i<6; i++) if(std::abs(m_playbackSpeed - speeds[i]) < 0.01) { current = i; break; }
            m_playbackSpeed = speeds[(current + 1) % 6];
            char buf[32]; std::snprintf(buf, sizeof(buf), "SPEED: %.2fx", m_playbackSpeed);
            m_pause.SetLabel(1, buf);
            return Transition::Handled();
        }
        if (action == PauseAction::ChangeSoundFont)  { OpenSoundFontDialog(ctx); return Transition::Handled(); }
        if (action == PauseAction::ChangeBackground) { OpenBackgroundDialog(ctx, m_backgroundTex); return Transition::Handled(); }
        if (action == PauseAction::OpenMidiFile) {
            std::wstring path = OpenMidiFileDialog(ctx.window->Handle());
            if (!path.empty()) LoadMidiFile(ctx, path);
            return Transition::Handled();
        }
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

    // Only handle control keys - let all other keys pass through for piano playing
    if (key == VK_ESCAPE) {
        char sbuf[32]; std::snprintf(sbuf, sizeof(sbuf), "SPEED: %.2fx", m_playbackSpeed);
        m_pause.SetLabel(1, sbuf);
        m_pause.SetLabel(4, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
        m_pause.Open();
        m_playing = false; // Pause playback
        ctx.audio->AllNotesOff();
        return Transition::Handled();
    }
    // Playback controls only when pause menu is NOT open
    if (key == VK_SPACE) {
        m_playing = !m_playing;
        // Re-anchor wall clock on resume so we continue from exactly where we paused.
        if (m_playing) {
            m_clockAnchor  = Clock::now();
            m_timeAtAnchor = m_playbackTime;
        }
        return Transition::Handled();
    }
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

    // All other keys (including piano keys A-K, W-U, O-P, Z-M) pass through to Input system
    return {};
}

Transition MidiPlaybackState::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    int vw = ctx.window->Width();
    int vh = ctx.window->Height();

    // Pause menu takes priority
    if (m_pause.IsOpen()) {
        PauseAction action = m_pause.OnMouse(x, y, down, move, vw, vh);
        if (action == PauseAction::Resume) {
            m_pause.Close();
            m_playing = true;
            m_clockAnchor = Clock::now();
            m_timeAtAnchor = m_playbackTime;
            return Transition::Handled();
        }
        if (action == PauseAction::ToggleSpeed) {
            static const double speeds[] = { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 };
            int current = 2;
            for(int i=0; i<6; i++) if(std::abs(m_playbackSpeed - speeds[i]) < 0.01) { current = i; break; }
            m_playbackSpeed = speeds[(current + 1) % 6];
            char buf[32]; std::snprintf(buf, sizeof(buf), "SPEED: %.2fx", m_playbackSpeed);
            m_pause.SetLabel(1, buf);
            return Transition::Handled();
        }
        if (action == PauseAction::ChangeSoundFont)  { OpenSoundFontDialog(ctx); return Transition::Handled(); }
        if (action == PauseAction::ChangeBackground) { OpenBackgroundDialog(ctx, m_backgroundTex); return Transition::Handled(); }
        if (action == PauseAction::OpenMidiFile) {
            std::wstring path = OpenMidiFileDialog(ctx.window->Handle());
            if (!path.empty()) LoadMidiFile(ctx, path);
            return Transition::Handled();
        }
        if (action == PauseAction::ToggleMode) {
            ctx.piano->ToggleDirection();
            m_pause.SetLabel(4, ctx.piano->IsFalling() ? "MODE: FALLING" : "MODE: RISING");
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
                float leadIn = 3.0f;
                float tailBuf = 3.0f;
                float totalDuration = (float)ctx.midi->Duration() + leadIn + tailBuf;
                float progress = std::clamp((x - tlX) / tlW, 0.0f, 1.0f);
                SeekTo(ctx, (double)progress * totalDuration - leadIn);
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
    // Allow negative time for lead-in buffer; clamp to [-duration, duration]
    double minT = -ctx.midi->Duration();
    m_playbackTime = std::clamp(newTime, minT, ctx.midi->Duration());
    m_playbackTick = (m_playbackTime >= 0) ? ctx.midi->SecondsToTicks(m_playbackTime) : 0;

    // Re-anchor wall clock so Update() resumes from the new position cleanly.
    m_clockAnchor  = Clock::now();
    m_timeAtAnchor = m_playbackTime;

    // Clear all active notes
    ctx.audio->AllNotesOff();
    ctx.noteState->AllNotesOff(ctx.timer->Elapsed());
    ctx.noteState->ClearVisualNotes();

    // Reset all controllers on all 16 channels to avoid stuck sustain/pedal after seek.
    for (int ch = 0; ch < 16; ch++) {
        ctx.audio->ControlChange(ch, 121, 0); // Reset All Controllers
        ctx.audio->ControlChange(ch, 64, 0);  // Sustain pedal off
        ctx.audio->ControlChange(ch, 66, 0);  // Sostenuto off
        ctx.audio->ControlChange(ch, 67, 0);  // Soft pedal off
    }

    // Fast-forward nextEventIdx and replay program/CC state up to seek point.
    m_nextEventIdx = 0;
    if (m_playbackTime > 0) {
        while (m_nextEventIdx < m_sortedEvents.size() && m_sortedEvents[m_nextEventIdx].time <= m_playbackTime) {
            auto& ev = m_sortedEvents[m_nextEventIdx];
            uint8_t type = ev.status & 0xF0;
            uint8_t chan = ev.globalChannel;

            if (type == 0xB0) ctx.audio->ControlChange(chan, ev.data1, ev.data2);
            else if (type == 0xC0) ctx.audio->ProgramChange(chan, ev.data1);
            else if (type == 0xE0) ctx.audio->PitchBend(chan, ev.data1 | (ev.data2 << 7));

            m_nextEventIdx++;
        }
    }
}

} // namespace pfd
