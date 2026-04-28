#include "GameLoop.h"
#include <string>
#include <format>

namespace pfd {

// Resize callback (static)
static GameLoop* s_gameLoop = nullptr;
static void ResizeCallback(int w, int h) { if (s_gameLoop) s_gameLoop->OnResize(w, h); }
static void KeyCallback(int k, bool d)   { if (s_gameLoop) s_gameLoop->OnKey(k, d); }

bool GameLoop::Initialize(HINSTANCE hInstance, int nCmdShow) {
    s_gameLoop = this;

    // Create window (1280x720 default)
    if (!m_window.Create(1280, 720, L"midiRoll")) return false;
    m_window.SetResizeCallback(ResizeCallback);
    m_window.SetKeyCallback(KeyCallback);

    // Initialize D3D11
    if (!m_d3d.Initialize(m_window.Handle(), m_window.Width(), m_window.Height())) return false;

    // Initialize renderer subsystems
    if (!m_spriteBatch.Initialize(m_d3d.Device())) return false;
    if (!m_textRenderer.Initialize(m_d3d.Device(), m_d3d.Context(), m_d3d.SwapChain())) return false;
    if (!m_bloom.Initialize(m_d3d.Device(), m_window.Width(), m_window.Height())) return false;
    if (!m_sceneRT.Create(m_d3d.Device(), m_window.Width(), m_window.Height())) return false;

    // Initialize piano renderer
    m_piano.Initialize(m_d3d.Device(), m_window.Width(), m_window.Height());

    // Initialize audio
    m_audio.Initialize();

    // Try to load a MIDI file from command line or default
    // For now, just start in interactive keyboard mode
    m_midiLoaded = false;
    m_playing = false;

    m_window.Show(nCmdShow);
    m_timer.Reset();

    return true;
}

void GameLoop::Run() {
    while (!m_window.ShouldClose()) {
        m_window.PumpMessages();

        double dt = m_timer.Delta();

        // FPS counter
        m_fpsAccum += dt;
        m_fpsCount++;
        if (m_fpsAccum >= 1.0) {
            m_fpsDisplay = m_fpsCount;
            m_fpsCount = 0;
            m_fpsAccum -= 1.0;
        }

        Update(dt);
        Render(0.0);
    }
}

void GameLoop::Shutdown() {
    m_audio.Shutdown();
}

void GameLoop::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    m_d3d.Resize(width, height);
    m_sceneRT.Create(m_d3d.Device(), width, height);
    m_bloom.Resize(m_d3d.Device(), width, height);
    m_piano.Resize(width, height);
}

void GameLoop::OnKey(int key, bool down) {
    m_input.OnKey(key, down);

    // Space = toggle playback
    if (key == VK_SPACE && down) {
        m_playing = !m_playing;
    }

    // Escape = quit
    if (key == VK_ESCAPE) {
        PostQuitMessage(0);
    }
}

void GameLoop::Update(double dt) {
    double currentTime = m_timer.Elapsed();

    // Process keyboard input → MIDI notes
    for (auto& ev : m_input.GetEvents()) {
        if (ev.isDown) {
            m_noteState.NoteOn(ev.note, 100, 0, currentTime);
            m_audio.PlayBeep(ev.note, 0.3f, 0.15f);
        } else {
            m_noteState.NoteOff(ev.note, 0, currentTime);
        }
    }
    m_input.ClearEvents();

    // MIDI file playback
    if (m_midiLoaded && m_playing) {
        m_playbackTime += dt;
        while (m_nextEventIdx < m_sortedEvents.size() &&
               m_sortedEvents[m_nextEventIdx].time <= m_playbackTime) {
            auto& ev = m_sortedEvents[m_nextEventIdx];
            if (ev.isNoteOn) {
                m_noteState.NoteOn(ev.data1, ev.data2, ev.status & 0x0F, currentTime);
                m_audio.PlayBeep(ev.data1, 0.2f, 0.1f);
            } else if (ev.isNoteOff) {
                m_noteState.NoteOff(ev.data1, ev.status & 0x0F, currentTime);
            }
            m_nextEventIdx++;
        }
    }

    // Update piano visuals + particles
    m_piano.Update(m_noteState, (float)currentTime, (float)dt);
    m_noteState.ClearRecentEvents();
}

void GameLoop::Render(double) {
    // Render scene to off-screen RT
    m_sceneRT.Bind(m_d3d.Context());
    m_sceneRT.Clear(m_d3d.Context(), 0.02f, 0.02f, 0.04f);

    float currentTime = (float)m_timer.Elapsed();

    // Draw everything via SpriteBatch
    m_spriteBatch.Begin(m_d3d.Context());
    m_piano.Render(m_spriteBatch, m_noteState, currentTime, (float)m_timer.Delta());
    m_spriteBatch.End(m_d3d.Context(), m_window.Width(), m_window.Height());

    // Apply bloom and composite to back buffer
    m_bloom.Apply(m_d3d.Context(), m_sceneRT.SRV(), m_d3d.BackBufferRTV(),
                  m_window.Width(), m_window.Height());

    // Text overlay (FPS + stats)
    m_textRenderer.BeginDraw(m_d3d.Context());
    auto fpsText = std::format(L"FPS: {} | Particles: {}", m_fpsDisplay, m_piano.particles.Count());
    m_textRenderer.DrawText(fpsText, 10, 10, 1.0f, 1.0f, 1.0f, 0.8f);

    auto controls = L"Keys: A-L / W-E-T-Y-U-O-P = play notes | SPACE = toggle playback | ESC = quit";
    m_textRenderer.DrawText(controls, 10, 40, 0.7f, 0.7f, 0.7f, 0.6f);
    m_textRenderer.EndDraw();

    m_d3d.Present(true);
}

} // namespace pfd
