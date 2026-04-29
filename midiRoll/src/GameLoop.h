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
#include <chrono>
#include <string>

namespace pfd {

// Menu state for the settings overlay
enum class MenuItem : int {
    LoadMidi = 0,
    LoadSoundFont,
    NoteSpeed,
    Direction,
    Volume,
    Resume,
    Quit,
    COUNT
};

class GameLoop {
public:
    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    void Run();
    void Shutdown();

    void OnResize(int width, int height);
    void OnKey(int key, bool down);
    void OnMouse(int x, int y, bool down, bool move);

private:
    void Update(double dt);
    void Render(double interpolation);
    void RenderMenu(SpriteBatch& batch, int viewW, int viewH);

    // File dialogs (Windows)
    std::wstring OpenFileDialog(const wchar_t* filter, const wchar_t* title);
    void LoadMidiFile(const std::wstring& path);
    void LoadSoundFontFile(const std::wstring& path);
    void TryAutoLoadSoundFont();

    // Window
    Window     m_window;

    // Rendering
    D3DContext    m_d3d;
    SpriteBatch   m_spriteBatch;

    // Audio & MIDI
    SoundFontEngine m_audio;
    MidiParser      m_midi;
    NoteState       m_noteState;

    // Game state
    PianoRenderer m_piano;
    FontRenderer  m_font;
    Input         m_input;
    util::Timer   m_timer;

    // Playback
    double  m_playbackTime{};
    bool    m_playing{};
    bool    m_midiLoaded{};
    std::vector<MidiEvent> m_sortedEvents;
    size_t  m_nextEventIdx{};
    uint32_t m_playbackTick{};  // Current position in MIDI ticks for tempo-aware playback

    // FPS
    double  m_fpsAccum{};
    int     m_fpsCount{};
    int     m_fpsDisplay{};

    // Menu state
    bool        m_menuOpen{};
    MenuItem    m_selectedItem{MenuItem::LoadMidi};
    std::array<bool, 128> m_mouseNotes{};

    // File paths for display
    std::string m_midiFilePath;
    std::string m_soundFontPath;
};

} // namespace pfd
