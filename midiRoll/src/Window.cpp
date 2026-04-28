#include "Window.h"
#include <stdexcept>

namespace pfd {

static Window* s_windowInstance = nullptr;

bool Window::Create(int width, int height, const wchar_t* title) {
    s_windowInstance = this;
    m_width = width;
    m_height = height;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName  = L"midiRoll";

    if (!RegisterClassExW(&wc)) return false;

    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0, L"midiRoll", title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    return m_hwnd != nullptr;
}

void Window::Show(int nCmdShow) {
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

void Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_shouldClose = true;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = s_windowInstance;
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_SIZE: {
        if (wp != SIZE_MINIMIZED) {
            self->m_width  = LOWORD(lp);
            self->m_height = HIWORD(lp);
            if (self->m_resizeCb)
                self->m_resizeCb(self->m_width, self->m_height);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (self->m_keyCb) self->m_keyCb((int)wp, true);
        return 0;
    case WM_KEYUP:
        if (self->m_keyCb) self->m_keyCb((int)wp, false);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace pfd
