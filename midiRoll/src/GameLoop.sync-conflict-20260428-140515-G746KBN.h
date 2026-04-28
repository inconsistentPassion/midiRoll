#pragma once
#include "Window.h"
#include "Renderer/D3DContext.h"
#include "Renderer/SpriteBatch.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/RenderTarget.h"
#include "Renderer/ShaderManager.h"
#include "Effects/Bloom.h"
#include "Piano/PianoRenderer.h"
#include "Piano/NoteState.h"
#include "Audio/AudioEngine.h"
#include "Audio/MidiParser.h"
#include "Input.h"
#include <chrono>

namespace pfd {

// High-resolution timer (inlined to avoid include issues)
class Timer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double>;

    Timer() : m_start(Clock::now()), m_last(Clock::now()) {}

    double Delta() {
        auto now = Clock::now();
        double dt = Duration(now - m_last).count();
        m_last = now;
        return (dt > 0.1) ? 0.1 : dt;
    }

    double Elapsed() const {
        return Duration(Clock::now() - m_start).count();
    }

    void Reset() {
        m_start = Clock::now();
        m_last = Clock::now();
    }

private:
    Clock::time_point m_start;
    Clock::time_point m_last;
};

class GameLoop {
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    void Run();
    void Shutdown();

    void OnResize(int width, int height);
    void OnKey(int key, bool down);

private:
    void Update(double dt);
    void Render(double interpolation);

    // Window
    Window     m_window;

    // Rendering
    D3DContext    m_d3d;
    SpriteBatch   m_spriteBatch;
    TextRenderer  m_textRenderer;
    Bloom         m_bloom;
    RenderTarget  m_sceneRT;

    // Audio & MIDI
    AudioEngine   m_audio;
    MidiParser    m_midi;
    NoteState     m_noteState;

    // Game state
    PianoRenderer m_piano;
    Input         m_input;
    util::Timer   m_timer;

    // Playback
    double  m_playbackTime{};
    bool    m_playing{};
    bool    m_midiLoaded{};
    std::vector<MidiEvent> m_sortedEvents;
    size_t  m_nextEventIdx{};

    // FPS
    double  m_fpsAccum{};
    int     m_fpsCount{};
    int     m_fpsDisplay{};
};

} // namespace pfd
