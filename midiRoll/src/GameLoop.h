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
#include "Util/Timer.h"

namespace pfd {

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
    Timer         m_timer;

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
