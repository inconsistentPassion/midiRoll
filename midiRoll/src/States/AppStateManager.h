#pragma once
#include "AppState.h"
#include <memory>
#include <unordered_map>

namespace pfd {

class AppStateManager {
public:
    void Register(StateID id, std::unique_ptr<AppState> state);

    void SwitchTo(StateID id, Context& ctx);
    void Update(Context& ctx, double dt);
    void Render(Context& ctx);

    // Returns true if a transition was requested
    bool OnKey(Context& ctx, int key, bool down);
    bool OnMouse(Context& ctx, int x, int y, bool down, bool move);

    StateID Current() const { return m_current; }

private:
    void ApplyTransition(const Transition& t, Context& ctx);

    std::unordered_map<StateID, std::unique_ptr<AppState>> m_states;
    StateID m_current{};
    AppState* m_active{};
};

} // namespace pfd
