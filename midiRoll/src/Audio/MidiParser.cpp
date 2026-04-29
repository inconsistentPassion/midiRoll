#include "MidiParser.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace pfd {

static uint16_t ReadU16BE(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t ReadU32BE(const uint8_t* p) { return (uint32_t)((p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]); }

static uint32_t ReadVarLen(const uint8_t*& ptr) {
    uint32_t val = 0;
    for (;;) {
        uint8_t b = *ptr++;
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

bool MidiParser::Load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    size_t size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return LoadFromMemory(data.data(), size);
}

bool MidiParser::LoadFromMemory(const uint8_t* data, size_t size) {
    if (size < 14) return false;
    const uint8_t* ptr = data;

    // Header: "MThd"
    if (memcmp(ptr, "MThd", 4) != 0) return false;
    ptr += 4;
    uint32_t headerLen = ReadU32BE(ptr); ptr += 4;
    uint16_t format    = ReadU16BE(ptr); ptr += 2;
    uint16_t numTracks = ReadU16BE(ptr); ptr += 2;
    m_ticksPerQuarter  = ReadU16BE(ptr); ptr += 2;
    (void)headerLen;

    m_tracks.clear();
    m_tracks.resize(numTracks);
    m_rawTempoEvents.clear();

    // Parse all tracks, collecting tempo events with their absolute tick positions
    for (uint16_t t = 0; t < numTracks; t++) {
        if (ptr + 8 > data + size) break;
        if (memcmp(ptr, "MTrk", 4) != 0) break;
        ptr += 4;
        uint32_t trackLen = ReadU32BE(ptr); ptr += 4;
        const uint8_t* trackEnd = ptr + trackLen;
        uint32_t trackTickAccum = 0;
        ParseTrack(ptr, trackEnd, m_tracks[t], trackTickAccum);
        ptr = trackEnd;
    }

    BuildTempoMap();
    CalculateAbsoluteTimes();
    BuildNoteList();
    return true;
}

void MidiParser::BuildNoteList() {
    m_notes.clear();
    for (const auto& track : m_tracks) {
        std::map<int, MidiEvent> active;
        for (const auto& ev : track.events) {
            if (ev.isNoteOn) {
                active[ev.data1] = ev;
            } else if (ev.isNoteOff) {
                auto it = active.find(ev.data1);
                if (it != active.end()) {
                    m_notes.push_back({it->second.time, ev.time, it->second.status & 0x0F, ev.data1, it->second.data2});
                    active.erase(it);
                }
            }
        }
    }
}

bool MidiParser::ParseTrack(const uint8_t*& ptr, const uint8_t* end, MidiTrack& track, uint32_t& trackTickAccum) {
    uint8_t runningStatus = 0;

    while (ptr < end) {
        uint32_t deltaTicks = ReadVarLen(ptr);
        trackTickAccum += deltaTicks;

        uint8_t status = *ptr;
        if (status & 0x80) {
            runningStatus = status;
            ptr++;
        } else {
            status = runningStatus;
        }

        MidiEvent ev{};
        ev.status = status;

        uint8_t type = status & 0xF0;

        if (type == 0x90 || type == 0x80) {
            ev.data1 = *ptr++;
            ev.data2 = *ptr++;
            ev.isNoteOn  = (type == 0x90 && ev.data2 > 0);
            ev.isNoteOff = (type == 0x80) || (type == 0x90 && ev.data2 == 0);
            // Store absolute ticks in time field for now; converted to seconds later
            ev.time = (double)trackTickAccum;
            track.events.push_back(ev);
        } else if (type == 0xA0 || type == 0xB0 || type == 0xE0) {
            ptr += 2;
        } else if (type == 0xC0 || type == 0xD0) {
            ptr += 1;
        } else if (status == 0xFF) {
            runningStatus = 0; // Clear running status for meta events
            uint8_t metaType = *ptr++;
            uint32_t metaLen = ReadVarLen(ptr);
            if (metaType == 0x51 && metaLen == 3) {
                // Tempo: microseconds per quarter
                uint32_t uspq = (ptr[0]<<16)|(ptr[1]<<8)|ptr[2];
                m_rawTempoEvents.push_back({trackTickAccum, uspq});
            }
            if (metaType == 0x03) {
                track.name = std::string((const char*)ptr, metaLen);
            }
            ptr += metaLen;
        } else if (status == 0xF0) {
            // SysEx
            uint32_t sysexLen = ReadVarLen(ptr);
            ptr += sysexLen;
        } else {
            // Skip unknown
            ptr++;
        }
    }
    return true;
}

void MidiParser::BuildTempoMap() {
    m_tempoMap.clear();

    // Sort raw tempo events by tick
    std::sort(m_rawTempoEvents.begin(), m_rawTempoEvents.end(),
        [](const RawTempoEvent& a, const RawTempoEvent& b) { return a.tick < b.tick; });

    // Default tempo: 120 BPM = 500000 us/quarter
    uint32_t defaultUsq = 500000;

    // If no tempo events, add one at tick 0
    if (m_rawTempoEvents.empty()) {
        m_rawTempoEvents.push_back({0, defaultUsq});
    }

    // If first tempo event is not at tick 0, insert default tempo at tick 0
    if (m_rawTempoEvents.front().tick != 0) {
        m_rawTempoEvents.insert(m_rawTempoEvents.begin(), {0, defaultUsq});
    }

    // Remove duplicate ticks (keep first)
    auto last = std::unique(m_rawTempoEvents.begin(), m_rawTempoEvents.end(),
        [](const RawTempoEvent& a, const RawTempoEvent& b) { return a.tick == b.tick; });
    m_rawTempoEvents.erase(last, m_rawTempoEvents.end());

    // Build the tempo map with precomputed absolute seconds
    double cumulativeSeconds = 0.0;
    uint32_t prevTick = 0;
    uint32_t prevUsq = m_rawTempoEvents[0].usPerQuarter;

    for (size_t i = 0; i < m_rawTempoEvents.size(); i++) {
        auto& entry = m_rawTempoEvents[i];

        // Accumulate seconds from previous tempo entry to this one
        if (i > 0) {
            uint32_t tickDelta = entry.tick - prevTick;
            double secondsPerTick = (double)prevUsq / (1000000.0 * m_ticksPerQuarter);
            cumulativeSeconds += tickDelta * secondsPerTick;
        }

        m_tempoMap.push_back({entry.tick, entry.usPerQuarter, cumulativeSeconds});

        prevTick = entry.tick;
        prevUsq = entry.usPerQuarter;
    }

    // Set BPM from first tempo
    m_bpm = (uint16_t)(60000000.0 / m_rawTempoEvents[0].usPerQuarter);
}

double MidiParser::TicksToSeconds(uint32_t tick) const {
    if (m_tempoMap.empty()) {
        // Fallback: 120 BPM
        return (double)tick * 500000.0 / (1000000.0 * m_ticksPerQuarter);
    }

    // Find the last tempo entry at or before this tick
    // Binary search for efficiency
    size_t lo = 0, hi = m_tempoMap.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (m_tempoMap[mid].tick <= tick) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // lo is now one past the last entry <= tick, so the relevant entry is lo-1
    size_t idx = (lo > 0) ? lo - 1 : 0;

    const auto& entry = m_tempoMap[idx];
    uint32_t tickDelta = tick - entry.tick;
    double secondsPerTick = (double)entry.usPerQuarter / (1000000.0 * m_ticksPerQuarter);
    return entry.secondsAtTick + tickDelta * secondsPerTick;
}

void MidiParser::CalculateAbsoluteTimes() {
    // Convert all event times from absolute ticks to seconds
    for (auto& track : m_tracks) {
        for (auto& ev : track.events) {
            uint32_t absTick = (uint32_t)ev.time;
            ev.time = TicksToSeconds(absTick);
        }
    }

    // Find duration
    m_duration = 0;
    for (auto& track : m_tracks) {
        if (!track.events.empty()) {
            m_duration = std::max(m_duration, track.events.back().time);
        }
    }
}

std::vector<MidiEvent> MidiParser::GetAllEventsSorted() const {
    std::vector<MidiEvent> all;
    for (const auto& track : m_tracks) {
        for (const auto& ev : track.events) {
            if (ev.isNoteOn || ev.isNoteOff) all.push_back(ev);
        }
    }
    std::sort(all.begin(), all.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.time < b.time;
    });
    return all;
}

} // namespace pfd
