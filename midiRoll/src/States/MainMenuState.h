#pragma once
#include "AppState.h"
#include "../Renderer/UIRenderer.h"
#include "../Util/Color.h"
#include <array>
#include <string>
#include <random>
#include <filesystem>

namespace pfd {

class MainMenuState : public AppState {
public:
    void Enter(Context& ctx) override;
    void Exit(Context& ctx) override;
    Transition Update(Context& ctx, double dt) override;
    void Render(Context& ctx) override;
    Transition OnKey(Context& ctx, int key, bool down) override;
    Transition OnMouse(Context& ctx, int x, int y, bool down, bool move) override;
    Transition OnMouseWheel(Context& ctx, int delta) override;

protected:
    // ── Button actions ──
public:
    enum class ButtonID {
        FreePlay, OpenMidi,
        Project0, Project1, Project2,
        SoundFont, MidiDevice, Settings,
        Count
    };

private:
    struct Button {
        float x, y, w, h;
        ButtonID id;
        bool hovered = false;
        float hoverAnim = 0.0f; // smooth 0..1
        float pressAnim = 0.0f; // smooth 0..1 for press animation
    };

    // ── Background note (uses GPUNoteSystem visuals) ──
    struct FallingNote {
        float x, y;
        float speed;
        float width, height;
        util::Color color;
        float alpha;
    };

    // ── Recent project entry ──
    struct RecentProject {
        std::string name;
        std::string date;
        std::string filePath;
        // Accurate note data for mini preview
        struct NoteBlock { float x, y, w, h; util::Color color; };
        std::vector<NoteBlock> notes;
        bool hovered = false;
        float hoverAnim = 0.0f; // smooth 0..1 for card expansion
    };

    // ── Draw helpers (all use UIRenderer, no SpriteBatch) ──
    void DrawBackground(Context& ctx);
    void DrawTitle(Context& ctx);
    void DrawMainButtons(Context& ctx);
    void DrawRecentProjects(Context& ctx);
    void DrawBottomBar(Context& ctx);
    void DrawHint(Context& ctx);
    void SpawnBackgroundNote(Context& ctx);
    
    // ── File and project logic ──
    void LoadRecentProjects();
    void SaveRecentProjects();
    void AddRecentProject(const std::string& path);
    void GenerateNotesFromMidi(RecentProject& proj, const std::string& path);

    // ── Button hit testing ──
    ButtonID HitTest(int mx, int my);
    void UpdateHoverStates(int mx, int my);
    Transition ActivateButton(Context& ctx, ButtonID id);
    void HandleRightClick(int mx, int my);
    void ShowContextMenu(HWND hWnd, int x, int y, int projectIndex);

    // ── Data ──
    std::array<Button, (int)ButtonID::Count> m_buttons{};
    ButtonID m_focused = ButtonID::FreePlay;

    std::vector<FallingNote> m_bgNotes;
    std::vector<RecentProject> m_recentProjects;
    std::mt19937 m_rng{std::random_device{}()};

    float m_spawnTimer = 0;
    float m_titleAnim = 0;
    float m_enterAnim = 1.0f;

    int m_saberColorIdx = 15;
    int m_midiDeviceIndex = -1;
    
    // Scroll offset for recent projects
    float m_projectScrollOffset = 0.0f;
    int m_maxVisibleProjects = 3;

    // Cached layout (recalculated on resize)
    int m_lastW = 0, m_lastH = 0;
    void RebuildLayout(int vw, int vh);
};

} // namespace pfd
