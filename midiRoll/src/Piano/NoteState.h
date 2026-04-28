#pragma once
#include <cstdint>
#include <array>
#include <vector>

namespace pfd {

// Track state of all 128 MIDI notes
struct NoteInfo {
    bool     active{};
    uint8_t  velocity{};    // 0-127
    uint8_t  channel{};
    double   onTime{};      // when note-on happened (seconds)
    double   offTime{};     // when note-off happened
    bool     visualActive{}; // still rendering (fading out)
};

class NoteState {
public:
    static constexpr int NOTE_COUNT = 128;

    void NoteOn(int note, int velocity, int channel, double time);
    void NoteOff(int note, int channel, double time);
    void AllNotesOff(double time);

    const NoteInfo& operator[](int note) const { return m_notes[note]; }
    NoteInfo& operator[](int note) { return m_notes[note]; }

    // Get notes that just turned on (for triggering effects)
    const std::vector<int>& RecentNoteOns() const { return m_recentOns; }
    const std::vector<int>& RecentNoteOffs() const { return m_recentOffs; }
    void ClearRecentEvents() { m_recentOns.clear(); m_recentOffs.clear(); }

    // Piano key range helpers
    static constexpr int FIRST_KEY = 21;  // A0
    static constexpr int LAST_KEY  = 108; // C8
    static bool IsBlackKey(int note);

private:
    std::array<NoteInfo, NOTE_COUNT> m_notes{};
    std::vector<int> m_recentOns;
    std::vector<int> m_recentOffs;
};

} // namespace pfd
