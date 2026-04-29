#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <map>

namespace pfd {

struct Note {
    double start;
    double end;
    int    channel;
    int    note;
    int    velocity;
};

struct MidiEvent {
    double   time;      // absolute time in seconds (for display/sorting)
    uint32_t tick;      // absolute tick position (for tempo-aware playback)
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

// Tempo change entry for the tempo map
struct TempoEntry {
    uint32_t tick;                  // absolute tick position
    uint32_t usPerQuarter;          // microseconds per quarter note at this tick
    double   secondsAtTick;         // precomputed absolute seconds at this tick
};

class MidiParser {
public:
    bool Load(const std::string& path);
    bool Load(const std::wstring& path);
    bool LoadFromMemory(const uint8_t* data, size_t size);

    const std::vector<MidiTrack>& Tracks() const { return m_tracks; }
    uint16_t TicksPerQuarter() const { return m_ticksPerQuarter; }
    double   Duration() const { return m_duration; }
    uint16_t BPM() const { return m_bpm; }

    // Get all note events sorted by time (for playback)
    std::vector<MidiEvent> GetAllEventsSorted() const;

    // Tempo map access
    const std::vector<TempoEntry>& TempoMap() const { return m_tempoMap; }
    const std::vector<Note>& Notes() const { return m_notes; }
    
    // Convert playback time (seconds) to MIDI ticks for tempo-aware seeking
    uint32_t SecondsToTicks(double seconds) const;

private:
    bool ParseTrack(const uint8_t*& ptr, const uint8_t* end, MidiTrack& track, uint32_t& trackTickAccum);
    void BuildTempoMap();
    void CalculateAbsoluteTimes();
    void BuildNoteList();
    double TicksToSeconds(uint32_t tick) const;

    std::vector<MidiTrack> m_tracks;
    uint16_t m_ticksPerQuarter = 480;
    bool     m_usingSmpte = false;
    uint8_t  m_smpteFps = 24;
    uint8_t  m_ticksPerFrame = 80;
    uint16_t m_bpm = 120;
    double   m_duration = 0;

    // Tempo map: sorted by tick, maps tick -> microseconds per quarter
    // All tempo events across all tracks (usually only track 0 in Format 1)
    struct RawTempoEvent {
        uint32_t tick;
        uint32_t usPerQuarter;
    };
    std::vector<RawTempoEvent>  m_rawTempoEvents;
    std::vector<TempoEntry>     m_tempoMap;
    std::vector<Note> m_notes;
};

} // namespace pfd
