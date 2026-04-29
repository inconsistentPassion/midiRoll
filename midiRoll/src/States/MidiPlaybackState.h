#pragma once
#include "AppState.h"
#include "PauseMenu.h"
#include <vector>
#include <chrono>

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

    // Wall-clock anchored playback: m_playbackTime = m_timeAtAnchor + (now - m_clockAnchor) * speed
    // This means frame-rate jitter never accumulates into timing drift.
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_clockAnchor{};   // wall-clock moment we last set the anchor
    double            m_timeAtAnchor{};  // playback time at that moment

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
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_backgroundTex;
    float m_backgroundDim{0.3f};
};

} // namespace pfd
