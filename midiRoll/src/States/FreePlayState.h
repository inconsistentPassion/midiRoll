#pragma once
#include "AppState.h"
#include "PauseMenu.h"

namespace pfd {

class FreePlayState : public AppState {
public:
    void Enter(Context& ctx) override;
    void Exit(Context& ctx) override;
    Transition Update(Context& ctx, double dt) override;
    void Render(Context& ctx) override;
    Transition OnKey(Context& ctx, int key, bool down) override;
    Transition OnMouse(Context& ctx, int x, int y, bool down, bool move) override;

private:
    void DrawHUD(Context& ctx);

    double     m_liveTime{};
    std::array<bool, 128> m_mouseNotes{};
    PauseMenu  m_pause;
    bool       m_showHUD{true};
};

} // namespace pfd
