#pragma once
#include "AppState.h"
#include "PauseMenu.h"
#include <vector>

namespace pfd {

class MidiPlaybackState : public AppState {
public:
    void Enter(Context& ctx) override;
    void Exit(Context& ctx) override;
    Transition Update(Context& ctx, double dt) override;
    void Render(Context& ctx) override;
    Transition OnKey(Context& ctx, int key, bool down) override;
    Transition OnMouse(Context& ctx, int x, int y, bool down, bool move) override;

private:
    void LoadMidiFile(Context& ctx, const std::wstring& path);
    void DrawTimeline(Context& ctx);
    void DrawControls(Context& ctx);
    void DrawLoadPrompt(Context& ctx);
    void SeekTo(Context& ctx, double newTime);

    double  m_playbackTime{};
    uint32_t m_playbackTick{};
    size_t  m_nextEventIdx{};
    bool    m_playing{};
    bool    m_loop{true};
    double  m_playbackSpeed{1.0};

    std::vector<MidiEvent> m_sortedEvents;

    PauseMenu m_pause;
    float   m_fpsAccum{};
    int     m_fpsCount{};
    int     m_fpsDisplay{};
    bool    m_draggingTimeline{};
    bool    m_showUI{true};
};

} // namespace pfd
