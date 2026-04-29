#include "AppStateManager.h"

namespace pfd {

void AppStateManager::Register(StateID id, std::unique_ptr<AppState> state) {
    m_states[id] = std::move(state);
}

void AppStateManager::SwitchTo(StateID id, Context& ctx) {
    if (m_active) m_active->Exit(ctx);
    m_current = id;
    m_active = m_states[id].get();
    m_active->Enter(ctx);
}

void AppStateManager::Update(Context& ctx, double dt) {
    if (!m_active) return;
    Transition t = m_active->Update(ctx, dt);
    ApplyTransition(t, ctx);
}

void AppStateManager::Render(Context& ctx) {
    if (m_active) m_active->Render(ctx);
}

bool AppStateManager::OnKey(Context& ctx, int key, bool down) {
    if (!m_active) return false;
    Transition t = m_active->OnKey(ctx, key, down);
    ApplyTransition(t, ctx);
    return t.handled || t.requested;
}

bool AppStateManager::OnMouse(Context& ctx, int x, int y, bool down, bool move) {
    if (!m_active) return false;
    Transition t = m_active->OnMouse(ctx, x, y, down, move);
    return ApplyTransition(t, ctx), t.requested;
}

void AppStateManager::ApplyTransition(const Transition& t, Context& ctx) {
    if (t.requested) SwitchTo(t.target, ctx);
}

} // namespace pfd
