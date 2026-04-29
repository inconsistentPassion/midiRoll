#pragma once
#include "Context.h"
#include "../Util/TextureLoader.h"
#include <Windows.h>
#include <commdlg.h>
#include <string>

namespace pfd {

// Opens a file dialog to pick a .sf2 SoundFont and loads it.
// Returns true if a file was chosen and loaded.
inline bool OpenSoundFontDialog(Context& ctx) {
    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = ctx.window->Handle();
    ofn.lpstrFilter = L"SoundFont Files (*.sf2)\0*.sf2\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Open SoundFont File";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;

    int nlen = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(nlen - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath, -1, narrow.data(), nlen, nullptr, nullptr);
    ctx.audio->LoadSoundFont(narrow);
    ctx.soundFontPath = narrow;
    return true;
}

// Opens a file dialog to pick an image for the background.
// Loads the texture into `outTex`. Returns true if a file was chosen and loaded.
inline bool OpenBackgroundDialog(Context& ctx,
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outTex)
{
    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = ctx.window->Handle();
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Open Background Image";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;

    auto srv = util::TextureLoader::LoadTextureFromFile(ctx.d3d->Device(), filePath);
    if (!srv) return false;
    outTex = srv;
    return true;
}

// Opens a file dialog to pick a MIDI file.
// Returns the file path if chosen, empty string if cancelled.
inline std::wstring OpenMidiFileDialog(HWND hwnd) {
    wchar_t filePath[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = L"MIDI Files (*.mid;*.midi)\0*.mid;*.midi\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Open MIDI File";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return L"";
    return filePath;
}

} // namespace pfd
