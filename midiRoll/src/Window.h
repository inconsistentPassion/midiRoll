#pragma once
#include <Windows.h>
#include <string>

namespace pfd {

// Callbacks for the game loop
using ResizeCallback = void(*)(int width, int height);
using KeyCallback    = void(*)(int key, bool down);
using MouseCallback  = void(*)(int x, int y, bool down, bool move);
using MidiCallback   = void(*)(int note, int velocity, bool noteOn);

class Window {
public:
    bool Create(int width, int height, const wchar_t* title);
    void Show(int nCmdShow);
    void SetResizeCallback(ResizeCallback cb) { m_resizeCb = cb; }
    void SetKeyCallback(KeyCallback cb) { m_keyCb = cb; }
    void SetMouseCallback(MouseCallback cb) { m_mouseCb = cb; }

    HWND  Handle() const { return m_hwnd; }
    int   Width()  const { return m_width; }
    int   Height() const { return m_height; }
    bool  ShouldClose() const { return m_shouldClose; }

    // Process pending Windows messages
    void PumpMessages();

    // Request close (sets shouldClose flag and posts WM_QUIT)
    void RequestClose() { m_shouldClose = true; PostQuitMessage(0); }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND          m_hwnd{};
    int           m_width{};
    int           m_height{};
    bool          m_shouldClose{};
    ResizeCallback m_resizeCb{};
    KeyCallback    m_keyCb{};
    MouseCallback  m_mouseCb{};
};

} // namespace pfd
