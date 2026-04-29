#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <unordered_map>

namespace pfd {

struct NoteInfo {
    bool     active{};
    uint8_t  velocity{};
    uint8_t  channel{};
    double   onTime{};
    double   offTime{};
    bool     visualActive{};
    uint64_t activeChannels{}; // Bitmask of channels currently playing this note
    uint64_t sustainChannels{}; // Bitmask of channels holding this note via sustain pedal
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

    // Sustain pedal (CC #64): holds notes visually after key release
    void SustainOn(int channel);
    void SustainOff(int channel, double time);

    const NoteInfo& operator[](int note) const { return m_keyState[note]; }
    
    const std::vector<ActiveVisualNote>& GetVisualNotes() const { return m_visualNotes; }
    void UpdateVisualNotes(double currentTime);
    void ClearVisualNotes(); // Wipes all rendering bars immediately

    const std::vector<int>& RecentNoteOns() const { return m_recentOns; }
    void ClearRecentEvents() { m_recentOns.clear(); }

    static constexpr int FIRST_KEY = 21;
    static constexpr int LAST_KEY  = 108;

private:
    // Hash key for (note, channel) pair
    static uint32_t MakeNoteKey(int note, int channel) {
        return (static_cast<uint32_t>(note) << 8) | static_cast<uint32_t>(channel);
    }
    
    std::array<NoteInfo, NOTE_COUNT> m_keyState{};
    std::vector<ActiveVisualNote> m_visualNotes;
    std::unordered_map<uint32_t, size_t> m_visualNoteIndex; // (note,channel) -> index in m_visualNotes
    std::vector<int> m_recentOns;

    // Sustain pedal state per channel (bitmask of 64 channels)
    uint64_t m_sustainActive{}; // bit set = sustain pedal down on that channel
};

} // namespace pfd
