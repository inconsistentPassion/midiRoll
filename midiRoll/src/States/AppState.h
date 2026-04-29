#pragma once
#include "Context.h"

namespace pfd {

enum class StateID {
    MainMenu,
    FreePlay,
    MidiPlayback,
};

// Return value from Update/HandleInput to request a state transition
struct Transition {
    StateID target{};
    bool    requested{};
    bool    handled{};

    Transition() = default;
    Transition(StateID t, bool r, bool h = false) : target(t), requested(r), handled(h) {}
    static Transition Handled() { return { {}, false, true }; }
};

class AppState {
public:
    virtual ~AppState() = default;

    // Called when entering this state
    virtual void Enter(Context& ctx) = 0;

    // Called when leaving this state
    virtual void Exit(Context& ctx) = 0;

    // Update game logic. Return a Transition to switch states.
    virtual Transition Update(Context& ctx, double dt) = 0;

    // Render the state
    virtual void Render(Context& ctx) = 0;

    // Handle raw input events. Return a Transition to switch states.
    virtual Transition OnKey(Context& ctx, int key, bool down) = 0;
    virtual Transition OnMouse(Context& ctx, int x, int y, bool down, bool move) = 0;
};

} // namespace pfd
