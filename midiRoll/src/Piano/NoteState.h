#pragma once
#include <cstdint>
#include <array>
#include <vector>

namespace pfd {

struct NoteInfo {
    bool     active{};
    uint8_t  velocity{};
    uint8_t  channel{};
    double   onTime{};
    double   offTime{};
    bool     visualActive{};
};

struct ActiveVisualNote {
    int    note;
    int    channel;
    int    velocity;
    double onTime;
    double offTime;
    bool   active;
};

class NoteState {
public:
    static constexpr int NOTE_COUNT = 128;

    void NoteOn(int note, int velocity, int channel, double time);
    void NoteOff(int note, int channel, double time);
    void AllNotesOff(double time);

    const NoteInfo& operator[](int note) const { return m_keyState[note]; }
    
    const std::vector<ActiveVisualNote>& GetVisualNotes() const { return m_visualNotes; }
    void UpdateVisualNotes(double currentTime);

    const std::vector<int>& RecentNoteOns() const { return m_recentOns; }
    void ClearRecentEvents() { m_recentOns.clear(); }

    static constexpr int FIRST_KEY = 21;
    static constexpr int LAST_KEY  = 108;

private:
    std::array<NoteInfo, NOTE_COUNT> m_keyState{};
    std::vector<ActiveVisualNote> m_visualNotes;
    std::vector<int> m_recentOns;
};

} // namespace pfd
