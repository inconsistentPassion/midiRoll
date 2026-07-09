#include "Window.h"
#include <stdexcept>

namespace pfd {

static Window* s_windowInstance = nullptr;

bool Window::Create(int width, int height, const wchar_t* title) {
    s_windowInstance = this;
    
    // Enforce 1080p minimum window size
    m_width = std::max(width, 1920);
    m_height = std::max(height, 1080);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName  = L"midiRoll";

    if (!RegisterClassExW(&wc)) return false;

    RECT rc = {0, 0, m_width, m_height};
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
            int newWidth = LOWORD(lp);
            int newHeight = HIWORD(lp);
            
            // Enforce 1080p minimum window size
            if (newWidth < 1920 || newHeight < 1080) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                newWidth = std::max(newWidth, 1920);
                newHeight = std::max(newHeight, 1080);
                
                // Adjust window size to maintain client area
                rc.right = rc.left + newWidth;
                rc.bottom = rc.top + newHeight;
                AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
                SetWindowPos(hwnd, nullptr, 0, 0, 
                           rc.right - rc.left, rc.bottom - rc.top,
                           SWP_NOMOVE | SWP_NOZORDER);
            }
            
            self->m_width  = newWidth;
            self->m_height = newHeight;
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
    case WM_LBUTTONDOWN:
        if (self->m_mouseCb) self->m_mouseCb(LOWORD(lp), HIWORD(lp), true, false);
        return 0;
    case WM_LBUTTONUP:
        if (self->m_mouseCb) self->m_mouseCb(LOWORD(lp), HIWORD(lp), false, false);
        return 0;
    case WM_RBUTTONDOWN:
        if (self->m_mouseCb) self->m_mouseCb(LOWORD(lp), HIWORD(lp), true, false);
        return 0;
    case WM_MOUSEMOVE:
        if (self->m_mouseCb) self->m_mouseCb(LOWORD(lp), HIWORD(lp), (wp & MK_LBUTTON) != 0, true);
        return 0;
    case WM_MOUSEWHEEL: {
        if (self->m_mouseWheelCb) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            self->m_mouseWheelCb(delta);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace pfd
