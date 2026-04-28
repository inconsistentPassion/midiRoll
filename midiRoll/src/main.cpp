#include <Windows.h>
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
#include "GameLoop.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Make the app DPI-aware so WM_SIZE reports physical pixels
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    pfd::GameLoop game;

    if (!game.Initialize(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to initialize midiRoll.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    game.Run();
    game.Shutdown();
    return 0;
}
