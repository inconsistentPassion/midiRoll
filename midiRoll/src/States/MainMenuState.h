#pragma once
#include "AppState.h"
#include "../Util/Color.h"
#include <array>
#include <string>
#include <random>

namespace pfd {

class MainMenuState : public AppState {
public:
    void Enter(Context& ctx) override;
    void Exit(Context& ctx) override;
    Transition Update(Context& ctx, double dt) override;
    void Render(Context& ctx) override;
    Transition OnKey(Context& ctx, int key, bool down) override;
    Transition OnMouse(Context& ctx, int x, int y, bool down, bool move) override;

private:
    enum class MenuAction { None, FreePlay, MidiPlayback, SoundFont, SaberColor, MidiDevice, Quit };

    struct MenuItem {
        std::string label;
        MenuAction  action;
        util::Color color;
        float       hoverAnim{}; // 0..1
    };

    struct FallingNote {
        float x, y;
        float speed;
        float width;
        float height;
        util::Color color;
        float alpha;
    };

    void DrawBackground(Context& ctx);
    void DrawTitle(Context& ctx);
    void DrawButtons(Context& ctx);
    void DrawHint(Context& ctx);
    void SpawnBackgroundNote(Context& ctx);

    // Returns a label like "MIDI: Piano [1/2]" or "MIDI: none"
    std::string BuildMidiLabel(int deviceIndex) const;

    // Cycles to next MIDI device and opens it; updates the button label
    void CycleMidiDevice(Context& ctx);
    void CycleSaberColor(Context& ctx);

    std::vector<MenuItem> m_items{{
        {"FREE PLAY",      MenuAction::FreePlay,     {0.3f, 0.8f, 1.0f, 1.0f}},
        {"MIDI PLAYBACK",  MenuAction::MidiPlayback, {1.0f, 0.6f, 0.2f, 1.0f}},
        {"SOUNDFONT",      MenuAction::SoundFont,    {0.8f, 0.7f, 0.4f, 1.0f}},
        {"SABER: WHITE",   MenuAction::SaberColor,   {0.9f, 0.9f, 0.9f, 1.0f}},
        {"MIDI: none",     MenuAction::MidiDevice,   {0.3f, 0.8f, 0.8f, 1.0f}},
        {"QUIT",           MenuAction::Quit,         {0.6f, 0.3f, 0.3f, 1.0f}},
    }};

    int m_selected{};
    int m_hovered{-1};
    int m_saberColorIdx{15};
    int m_midiDeviceIndex{-1}; // currently selected device (-1 = none)
    std::vector<FallingNote> m_bgNotes;
    std::mt19937 m_rng{std::random_device{}()};
    float m_spawnTimer{};
    float m_titleAnim{};
    float m_enterAnim{1.0f}; // fade-in on enter
};

} // namespace pfd
