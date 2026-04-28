#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace pfd {

struct MidiEvent {
    double   time;      // absolute time in seconds
    uint8_t  status;    // status byte (event type + channel)
    uint8_t  data1;     // note number / controller
    uint8_t  data2;     // velocity / value
    bool     isNoteOn;
    bool     isNoteOff;
};

struct MidiTrack {
    std::string name;
    std::vector<MidiEvent> events;
};

class MidiParser {
public:
    bool Load(const std::string& path);
    bool LoadFromMemory(const uint8_t* data, size_t size);

    const std::vector<MidiTrack>& Tracks() const { return m_tracks; }
    uint16_t TicksPerQuarter() const { return m_ticksPerQuarter; }
    double   Duration() const { return m_duration; }
    uint16_t BPM() const { return m_bpm; }

    // Get all note events sorted by time (for playback)
    std::vector<MidiEvent> GetAllEventsSorted() const;

private:
    bool ParseTrack(const uint8_t*& ptr, const uint8_t* end, MidiTrack& track);
    void CalculateAbsoluteTimes();

    std::vector<MidiTrack> m_tracks;
    uint16_t m_ticksPerQuarter = 480;
    uint16_t m_bpm = 120;
    double   m_duration = 0;
    std::vector<uint32_t> m_tempoMap; // tick -> microseconds per quarter
};

} // namespace pfd
