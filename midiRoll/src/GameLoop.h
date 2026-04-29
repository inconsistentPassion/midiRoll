#pragma once
#include "Window.h"
#include "Renderer/D3DContext.h"
#include "Renderer/SpriteBatch.h"
#include "Renderer/FontRenderer.h"
#include "Piano/PianoRenderer.h"
#include "Piano/NoteState.h"
#include "Audio/SoundFontEngine.h"
#include "Audio/MidiParser.h"
#include "Util/Math.h"
#include "Util/Color.h"
#include "Util/Timer.h"
#include "Input.h"
#include "Input/MidiInput.h"
#include "States/Context.h"
#include "States/AppStateManager.h"
#include <string>

namespace pfd {

class GameLoop {
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    void Run();
    void Shutdown();

    void OnResize(int width, int height);
    void OnKey(int key, bool down);
    void OnMouse(int x, int y, bool down, bool move);

private:
    // Core systems
    Window          m_window;
    D3DContext      m_d3d;
    SpriteBatch     m_spriteBatch;
    FontRenderer    m_font;
    PianoRenderer   m_piano;
    NoteState       m_noteState;
    SoundFontEngine m_audio;
    MidiParser      m_midi;
    Input           m_input;
    MidiInput       m_midiInput;
    util::Timer     m_timer;

    // State machine
    Context         m_ctx;
    AppStateManager m_states;

    // Auto-load SoundFont on startup
    void TryAutoLoadSoundFont();

    // Reconnect MIDI if device was chosen but lost
    void PollMidiReconnect(double dt);
    double m_midiReconnectTimer{0.0};
};

} // namespace pfd
