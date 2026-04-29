#include "GameLoop.h"
#include "Util/Math.h"
#include <string>
#include <format>
#include <Windows.h>
#include <commdlg.h>
#include <filesystem>

namespace pfd {

static GameLoop* s_gameLoop = nullptr;

// === GameLoop implementation ===

bool GameLoop::Initialize(HINSTANCE hInstance, int nCmdShow) {
    s_gameLoop = this;
    (void)hInstance;

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
    m_window.SetMouseCallback([](int x, int y, bool d, bool m) {
        if (s_gameLoop) s_gameLoop->OnMouse(x, y, d, m);
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
    m_font.Initialize(m_d3d.Device(), m_d3d.Context(), 20.0f);
    if (!m_audio.Initialize()) {
        // Fallback initialization should always succeed if XAudio2 is available
    }
    TryAutoLoadSoundFont();

    m_window.Show(nCmdShow);
    m_timer.Reset();
    return true;
}

void GameLoop::OnMouse(int x, int y, bool down, bool move) {
    if (m_menuOpen) {
        if (!down || move) return;
        // Menu item selection
        float panelW = 500.0f, panelH = 420.0f;
        float panelX = (m_window.Width() - panelW) * 0.5f;
        float panelY = (m_window.Height() - panelH) * 0.5f;
        float itemH = 48.0f, itemPad = 6.0f;
        float startY = panelY + 30.0f;

        if (x >= panelX && x <= panelX + panelW) {
            for (int i = 0; i < (int)MenuItem::COUNT; i++) {
                float iy = startY + i * (itemH + itemPad);
                if (y >= iy && y <= iy + itemH) {
                    m_selectedItem = static_cast<MenuItem>(i);
                    // Simulate Enter key press
                    OnKey(VK_RETURN, true);
                    break;
                }
            }
        }
        return;
    }

    // Piano interaction
    if (y >= m_piano.GetPianoY() && y <= m_window.Height()) {
        for (int n = NoteState::FIRST_KEY; n <= NoteState::LAST_KEY; n++) {
            float kx = m_piano.GetKeyX(n);
            float kw = m_piano.GetKeyWidth(n);
            if (x >= kx && x <= kx + kw) {
                if (down && !move) {
                    m_noteState.NoteOn(n, 100, 0, m_timer.Elapsed());
                    m_audio.NoteOn(0, n, 100);
                } else if (!down && !move) {
                    m_noteState.NoteOff(n, 0, m_timer.Elapsed());
                    m_audio.NoteOff(0, n);
                }
                break;
            }
        }
    }
}

void GameLoop::Run() {
    while (!m_window.ShouldClose()) {
        m_window.PumpMessages();
        double dt = m_timer.Delta();
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
    m_piano.Resize(width, height);
}

void GameLoop::OnKey(int key, bool down) {
    if (!down) return;

    if (key == VK_ESCAPE) {
        m_menuOpen = !m_menuOpen;
        if (m_menuOpen) m_audio.AllNotesOff();
        return;
    }

    if (m_menuOpen) {
        int sel = static_cast<int>(m_selectedItem);
        int count = static_cast<int>(MenuItem::COUNT);

        if (key == VK_UP) {
            sel = (sel - 1 + count) % count;
            m_selectedItem = static_cast<MenuItem>(sel);
            return;
        }
        if (key == VK_DOWN) {
            sel = (sel + 1) % count;
            m_selectedItem = static_cast<MenuItem>(sel);
            return;
        }
        if (key == VK_LEFT || key == VK_RIGHT) {
            float step = (key == VK_RIGHT) ? 1.0f : -1.0f;
            if (m_selectedItem == MenuItem::NoteSpeed) {
                float speed = m_piano.GetNoteSpeed() + step * 50.0f;
                m_piano.SetNoteSpeed(std::clamp(speed, 50.0f, 2000.0f));
            } else if (m_selectedItem == MenuItem::Volume) {
                float vol = m_audio.GetVolume() + step * 0.05f;
                m_audio.SetVolume(std::clamp(vol, 0.0f, 1.0f));
            }
            return;
        }
        if (key == VK_RETURN || key == VK_SPACE) {
            switch (m_selectedItem) {
            case MenuItem::LoadMidi: {
                std::wstring path = OpenFileDialog(
                    L"MIDI Files (*.mid;*.midi)\0*.mid;*.midi\0All Files (*.*)\0*.*\0",
                    L"Open MIDI File");
                if (!path.empty()) LoadMidiFile(path);
                break;
            }
            case MenuItem::LoadSoundFont: {
                std::wstring path = OpenFileDialog(
                    L"SoundFont Files (*.sf2)\0*.sf2\0All Files (*.*)\0*.*\0",
                    L"Load SoundFont");
                if (!path.empty()) LoadSoundFontFile(path);
                break;
            }
            case MenuItem::Direction:  
                m_piano.ToggleDirection(); 
                m_piano.Resize(m_window.Width(), m_window.Height());
                break;
            case MenuItem::Resume:     m_menuOpen = false; break;
            case MenuItem::Quit:       m_window.RequestClose(); break;
            default: break;
            }
            return;
        }
        return;
    }

    if (key == VK_SPACE) m_playing = !m_playing;
}

void GameLoop::Update(double dt) {
    if (m_menuOpen) { m_audio.ProcessEvents(); return; }

    double currentTime = m_timer.Elapsed();

    for (auto& ev : m_input.GetEvents()) {
        if (ev.isDown) {
            m_noteState.NoteOn(ev.note, 100, 0, currentTime);
            m_audio.NoteOn(0, ev.note, 100);
        } else {
            m_noteState.NoteOff(ev.note, 0, currentTime);
            m_audio.NoteOff(0, ev.note);
        }
    }
    m_input.ClearEvents();

    if (m_midiLoaded && m_playing) {
        m_playbackTime += dt;
        while (m_nextEventIdx < m_sortedEvents.size() &&
               m_sortedEvents[m_nextEventIdx].time <= m_playbackTime) {
            auto& ev = m_sortedEvents[m_nextEventIdx];
            if (ev.isNoteOn) {
                m_noteState.NoteOn(ev.data1, ev.data2, ev.status & 0x0F, currentTime);
                m_audio.NoteOn(ev.status & 0x0F, ev.data1, ev.data2);
            } else if (ev.isNoteOff) {
                m_noteState.NoteOff(ev.data1, ev.status & 0x0F, currentTime);
                m_audio.NoteOff(ev.status & 0x0F, ev.data1);
            }
            m_nextEventIdx++;
        }
    }

    m_audio.ProcessEvents();
    m_piano.Update(m_noteState, (float)currentTime, (float)dt);
    m_noteState.ClearRecentEvents();
}

void GameLoop::Render(double) {
    float currentTime = (float)m_timer.Elapsed();
    auto ctx = m_d3d.Context();

    // 1. Draw scene directly to back buffer
    auto* rtv = m_d3d.BackBufferRTV();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    
    D3D11_VIEWPORT vp{};
    vp.Width = (float)m_window.Width();
    vp.Height = (float)m_window.Height();
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    float clearColor[] = {0.02f, 0.02f, 0.04f, 1.0f};
    ctx->ClearRenderTargetView(rtv, clearColor);

    m_spriteBatch.Begin(ctx, m_window.Width(), m_window.Height());
    
    // We pass the "live" time for everything, but the MIDI notes need to be 
    // interpreted relative to the current playback position.
    m_piano.Render(m_spriteBatch, m_noteState, m_midi.Notes(), currentTime, (float)m_playbackTime, (float)m_timer.Delta());
    
    // 2. Draw UI/Menu
    if (m_menuOpen) {
        RenderMenu(m_spriteBatch, m_window.Width(), m_window.Height());
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "FPS: %d", m_fpsDisplay);
        m_font.DrawText(m_spriteBatch, buf, 10, 10, 0.8f, 0.8f, 0.8f, 0.8f);
        
        if (m_midiLoaded) {
            std::filesystem::path p(m_midiFilePath);
            m_font.DrawText(m_spriteBatch, p.filename().string(), 10, 35, 0.6f, 0.6f, 0.6f, 0.7f);
        }
    }
    m_spriteBatch.End();

    m_d3d.Present(true);
}

void GameLoop::RenderMenu(SpriteBatch& batch, int viewW, int viewH) {
    batch.SetTexture(m_font.GetTexture());
    batch.Draw({0, 0}, {(float)viewW, (float)viewH}, {0.0f, 0.0f, 0.0f, 0.7f});

    float panelW = 500.0f, panelH = 420.0f;
    float panelX = ((float)viewW - panelW) * 0.5f;
    float panelY = ((float)viewH - panelH) * 0.5f;

    batch.Draw({panelX, panelY}, {panelW, panelH}, {0.08f, 0.08f, 0.12f, 0.95f});
    float b = 2.0f;
    batch.Draw({panelX, panelY}, {panelW, b}, {0.3f, 0.6f, 1.0f, 0.8f});
    batch.Draw({panelX, panelY + panelH - b}, {panelW, b}, {0.3f, 0.6f, 1.0f, 0.8f});
    batch.Draw({panelX, panelY}, {b, panelH}, {0.3f, 0.6f, 1.0f, 0.8f});
    batch.Draw({panelX + panelW - b, panelY}, {b, panelH}, {0.3f, 0.6f, 1.0f, 0.8f});

    float itemH = 48.0f, itemPad = 6.0f;
    float startY = panelY + 30.0f, textX = panelX + 30.0f;
    float valueX = panelX + panelW - 180.0f;
    int sel = static_cast<int>(m_selectedItem);
    int count = static_cast<int>(MenuItem::COUNT);

    for (int i = 0; i < count; i++) {
        float y = startY + i * (itemH + itemPad);
        bool isSel = (i == sel);

        if (isSel) {
            batch.Draw({panelX + 4, y}, {panelW - 8, itemH}, {0.2f, 0.4f, 0.8f, 0.4f});
            batch.Draw({panelX + 4, y}, {4.0f, itemH}, {0.4f, 0.7f, 1.0f, 1.0f});
        } else {
            batch.Draw({panelX + 4, y}, {panelW - 8, itemH}, {0.12f, 0.12f, 0.16f, 0.6f});
        }

        const char* label = "???";
        float tR = 0.9f, tG = 0.9f, tB = 0.9f;
        switch (static_cast<MenuItem>(i)) {
        case MenuItem::LoadMidi:       label = "Load MIDI File";  tR=0.3f; tG=0.8f; tB=0.3f; break;
        case MenuItem::LoadSoundFont:  label = "Load SoundFont";  tR=0.8f; tG=0.6f; tB=0.2f; break;
        case MenuItem::NoteSpeed:      label = "Note Speed";      tR=0.3f; tG=0.6f; tB=1.0f; break;
        case MenuItem::Direction:      label = "Direction";       tR=0.8f; tG=0.3f; tB=0.8f; break;
        case MenuItem::Volume:         label = "Volume";          tR=1.0f; tG=0.8f; tB=0.2f; break;
        case MenuItem::Resume:         label = "Resume";          tR=0.3f; tG=1.0f; tB=0.5f; break;
        case MenuItem::Quit:           label = "Quit";            tR=1.0f; tG=0.3f; tB=0.3f; break;
        default: break;
        }
        float textY = y + (itemH - 7.0f * 2.5f) * 0.5f;
        m_font.DrawText(batch, label, textX, textY, tR, tG, tB, 1.0f);

        // NoteSpeed slider
        if (static_cast<MenuItem>(i) == MenuItem::NoteSpeed) {
            float barW = 130.0f, barH = 8.0f;
            float barY = y + (itemH - barH) * 0.5f;
            batch.Draw({valueX, barY}, {barW, barH}, {0.2f, 0.2f, 0.3f, 0.8f});
            float fill = std::clamp((m_piano.GetNoteSpeed() - 50.0f) / 1950.0f, 0.0f, 1.0f);
            batch.Draw({valueX, barY}, {barW * fill, barH}, {0.3f, 0.6f, 1.0f, 0.9f});
            batch.Draw({valueX + barW * fill - 3, barY - 2}, {6, barH + 4}, {0.6f, 0.8f, 1.0f, 1.0f});
            char buf[16]; snprintf(buf, sizeof(buf), "%d", (int)m_piano.GetNoteSpeed());
            m_font.DrawText(batch, buf, valueX + barW + 8, barY - 2, 0.7f, 0.8f, 1.0f, 1.0f);
        }

        // Volume slider
        if (static_cast<MenuItem>(i) == MenuItem::Volume) {
            float barW = 130.0f, barH = 8.0f;
            float barY = y + (itemH - barH) * 0.5f;
            batch.Draw({valueX, barY}, {barW, barH}, {0.2f, 0.2f, 0.3f, 0.8f});
            float fill = m_audio.GetVolume();
            batch.Draw({valueX, barY}, {barW * fill, barH}, {1.0f, 0.8f, 0.2f, 0.9f});
            batch.Draw({valueX + barW * fill - 3, barY - 2}, {6, barH + 4}, {1.0f, 0.9f, 0.5f, 1.0f});
            char buf[16]; snprintf(buf, sizeof(buf), "%d%%", (int)(m_audio.GetVolume() * 100));
            m_font.DrawText(batch, buf, valueX + barW + 8, barY - 2, 1.0f, 0.9f, 0.5f, 1.0f);
        }

        // Direction arrow + text
        if (static_cast<MenuItem>(i) == MenuItem::Direction) {
            float arrowX = valueX + 40.0f, arrowY = y + (itemH - 12) * 0.5f;
            if (m_piano.IsFalling()) {
                batch.Draw({arrowX, arrowY}, {12, 4}, {0.8f, 0.8f, 0.8f, 1});
                batch.Draw({arrowX + 2, arrowY + 4}, {8, 4}, {0.8f, 0.8f, 0.8f, 1});
                batch.Draw({arrowX + 4, arrowY + 8}, {4, 4}, {0.8f, 0.8f, 0.8f, 1});
            } else {
                batch.Draw({arrowX + 4, arrowY}, {4, 4}, {0.8f, 0.8f, 0.8f, 1});
                batch.Draw({arrowX + 2, arrowY + 4}, {8, 4}, {0.8f, 0.8f, 0.8f, 1});
                batch.Draw({arrowX, arrowY + 8}, {12, 4}, {0.8f, 0.8f, 0.8f, 1});
            }
            m_font.DrawText(batch, m_piano.IsFalling() ? "FALLING" : "RISING", arrowX + 18, arrowY, 0.8f, 0.8f, 0.8f, 1.0f);
        }
    }

    float footerY = panelY + panelH - 22.0f;
    batch.Draw({panelX + 20, footerY}, {panelW - 40, 1.0f}, {0.3f, 0.3f, 0.4f, 0.5f});
    batch.SetTexture(nullptr);
}

std::wstring GameLoop::OpenFileDialog(const wchar_t* filter, const wchar_t* title) {
    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_window.Handle();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) return std::wstring(filePath);
    return {};
}

void GameLoop::LoadMidiFile(const std::wstring& path) {
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), len, nullptr, nullptr);

    if (m_midi.Load(narrow)) {
        m_midiLoaded = true;
        m_sortedEvents = m_midi.GetAllEventsSorted();
        m_nextEventIdx = 0;
        m_playbackTime = 0;
        m_playing = true;
        m_midiFilePath = narrow;
        m_noteState.AllNotesOff(m_timer.Elapsed());
        m_audio.AllNotesOff();

        char buf[256];
        snprintf(buf, sizeof(buf), "MIDI Loaded: %s (%zu events, duration %.1fs)\n", 
                 narrow.c_str(), m_sortedEvents.size(), (double)m_midi.Duration());
        OutputDebugStringA(buf);
    } else {
        OutputDebugStringA("Failed to load MIDI file.\n");
    }
}

void GameLoop::LoadSoundFontFile(const std::wstring& path) {
    int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow.data(), len, nullptr, nullptr);
    if (m_audio.LoadSoundFont(narrow)) m_soundFontPath = narrow;
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
            std::string p = d + "\\" + names[n];
            if (std::filesystem::exists(p)) {
                if (m_audio.LoadSoundFont(p)) return;
            }
        }
    }
    for (auto& d : dirs) {
        try {
            for (auto& e : std::filesystem::directory_iterator(d)) {
                if (e.path().extension() == ".sf2") {
                    if (m_audio.LoadSoundFont(e.path().string())) return;
                }
            }
        } catch (...) {}
    }
}

} // namespace pfd
