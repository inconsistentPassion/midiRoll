#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "GameLoop.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    pfd::GameLoop game;

    if (!game.Initialize(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to initialize midiRoll.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    game.Run();
    game.Shutdown();
    return 0;
}
