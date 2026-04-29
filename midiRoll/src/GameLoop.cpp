#include "GameLoop.h"
#include "States/MainMenuState.h"
#include "States/FreePlayState.h"
#include "States/MidiPlaybackState.h"
#include <Windows.h>
#include <filesystem>

namespace pfd {

static GameLoop* s_gameLoop = nullptr;

bool GameLoop::Initialize(HINSTANCE hInstance, int nCmdShow) {
    s_gameLoop = this;

    if (!m_window.Create(1280, 720, L"midiRoll")) {
        MessageBoxW(nullptr, L"Failed: Window.Create", L"Init Error", MB_OK);
        return false;
    }

    m_window.SetResizeCallback([](int w, int h) {
        if (s_gameLoop) s_gameLoop->OnResize(w, h);
    });
    m_window.SetKeyCallback([](int k, bool d) {
        if (s_gameLoop) s_gameLoop->OnKey(k, d);
    });
    m_window.SetMouseCallback([](int x, int y, bool down, bool move) {
        if (s_gameLoop) s_gameLoop->OnMouse(x, y, down, move);
    });

    if (!m_d3d.Initialize(m_window.Handle(), m_window.Width(), m_window.Height())) {
        MessageBoxW(nullptr, L"Failed: D3D.Initialize", L"Init Error", MB_OK);
        return false;
    }
    if (!m_spriteBatch.Initialize(m_d3d.Device())) {
        MessageBoxW(nullptr, L"Failed: SpriteBatch.Initialize", L"Init Error", MB_OK);
        return false;
    }
    m_piano.Initialize(m_d3d.Device(), m_window.Width(), m_window.Height());
    m_font.Initialize(m_d3d.Device(), m_d3d.Context(), 48.0f);
    m_audio.Initialize();
    TryAutoLoadSoundFont();

    // Build shared context
    m_ctx.window      = &m_window;
    m_ctx.d3d         = &m_d3d;
    m_ctx.spriteBatch = &m_spriteBatch;
    m_ctx.font        = &m_font;
    m_ctx.piano       = &m_piano;
    m_ctx.noteState   = &m_noteState;
    m_ctx.audio       = &m_audio;
    m_ctx.midi        = &m_midi;
    m_ctx.input       = &m_input;
    m_ctx.timer       = &m_timer;

    // Register states
    m_states.Register(StateID::MainMenu,     std::make_unique<MainMenuState>());
    m_states.Register(StateID::FreePlay,     std::make_unique<FreePlayState>());
    m_states.Register(StateID::MidiPlayback, std::make_unique<MidiPlaybackState>());

    // Start at main menu
    m_states.SwitchTo(StateID::MainMenu, m_ctx);

    m_window.Show(nCmdShow);
    m_timer.Reset();
    return true;
}

void GameLoop::Run() {
    while (!m_window.ShouldClose()) {
        m_window.PumpMessages();
        double dt = m_timer.Delta();
        m_states.Update(m_ctx, dt);
        m_states.Render(m_ctx);
        m_d3d.Present(true);
    }
}

void GameLoop::Shutdown() {
    m_audio.Shutdown();
}

void GameLoop::OnResize(int width, int height) {
    if (width == 0 || height == 0) return;
    m_d3d.Resize(width, height);
    m_piano.Resize(width, height);
}

void GameLoop::OnKey(int key, bool down) {
    if (m_states.OnKey(m_ctx, key, down)) return;
    m_input.OnKey(key, down);
}

void GameLoop::OnMouse(int x, int y, bool down, bool move) {
    m_states.OnMouse(m_ctx, x, y, down, move);
}

void GameLoop::TryAutoLoadSoundFont() {
    std::vector<std::string> dirs = { ".", "libs" };

    char* up = nullptr;
    size_t ulen = 0;
    if (_dupenv_s(&up, &ulen, "USERPROFILE") == 0 && up) {
        std::string u(up);
        dirs.push_back(u + "\\Downloads");
        dirs.push_back(u + "\\Documents");
        dirs.push_back(u + "\\Music");
        dirs.push_back(u + "\\Desktop");
        free(up);
    }

    const char* names[] = {
        "FluidR3_GM.sf2", "FluidR3.sf2", "GeneralUser.sf2",
        "TimGM6.sf2", "MuseScore_General.sf2", "default.sf2", nullptr
    };

    for (auto& d : dirs) {
        for (int n = 0; names[n]; n++) {
            auto p = (std::filesystem::path(d) / names[n]).string();
            if (std::filesystem::exists(p)) {
                if (m_audio.LoadSoundFont(p)) {
                    m_ctx.soundFontPath = p;
                    return;
                }
            }
        }
    }
    for (auto& d : dirs) {
        try {
            for (auto& e : std::filesystem::directory_iterator(d)) {
                if (e.path().extension() == ".sf2") {
                    std::wstring ws = e.path().wstring();
                    int nlen = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    std::string u8(nlen - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, u8.data(), nlen, nullptr, nullptr);
                    if (m_audio.LoadSoundFont(u8)) {
                        m_ctx.soundFontPath = u8;
                        return;
                    }
                }
            }
        } catch (...) {}
    }
}

} // namespace pfd
