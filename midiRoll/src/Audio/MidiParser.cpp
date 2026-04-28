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

    for (uint16_t t = 0; t < numTracks; t++) {
        if (ptr + 8 > data + size) break;
        if (memcmp(ptr, "MTrk", 4) != 0) break;
        ptr += 4;
        uint32_t trackLen = ReadU32BE(ptr); ptr += 4;
        const uint8_t* trackEnd = ptr + trackLen;
        ParseTrack(ptr, trackEnd, m_tracks[t]);
        ptr = trackEnd;
    }

    CalculateAbsoluteTimes();
    return true;
}

bool MidiParser::ParseTrack(const uint8_t*& ptr, const uint8_t* end, MidiTrack& track) {
    uint8_t runningStatus = 0;

    while (ptr < end) {
        uint32_t deltaTicks = ReadVarLen(ptr);
        (void)deltaTicks; // we'll convert to seconds later via tempo map

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
            // Store raw ticks in time for now, convert later
            ev.time = (double)deltaTicks;
            track.events.push_back(ev);
        } else if (type == 0xA0 || type == 0xB0 || type == 0xE0) {
            ptr += 2;
        } else if (type == 0xC0 || type == 0xD0) {
            ptr += 1;
        } else if (status == 0xFF) {
            uint8_t metaType = *ptr++;
            uint32_t metaLen = ReadVarLen(ptr);
            if (metaType == 0x51 && metaLen == 3) {
                // Tempo: microseconds per quarter
                uint32_t uspq = (ptr[0]<<16)|(ptr[1]<<8)|ptr[2];
                m_tempoMap.push_back(uspq);
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

void MidiParser::CalculateAbsoluteTimes() {
    // Default tempo: 120 BPM = 500000 us per quarter
    uint32_t uspq = 500000;
    double secondsPerTick = (double)uspq / (1000000.0 * m_ticksPerQuarter);

    for (auto& track : m_tracks) {
        double absTime = 0;
        uint32_t tempoIdx = 0;
        for (auto& ev : track.events) {
            double deltaTicks = ev.time; // stored as raw ticks
            absTime += deltaTicks * secondsPerTick;
            ev.time = absTime;
        }
    }

    // Find duration
    m_duration = 0;
    for (auto& track : m_tracks) {
        if (!track.events.empty()) {
            m_duration = std::max(m_duration, track.events.back().time);
        }
    }

    // Estimate BPM from first tempo event
    if (!m_tempoMap.empty()) {
        m_bpm = (uint16_t)(60000000.0 / m_tempoMap[0]);
    }
}

std::vector<MidiEvent> MidiParser::GetAllEventsSorted() const {
    std::vector<MidiEvent> all;
    for (auto& track : m_tracks) {
        for (auto& ev : track.events) {
            if (ev.isNoteOn || ev.isNoteOff) all.push_back(ev);
        }
    }
    std::sort(all.begin(), all.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.time < b.time;
    });
    return all;
}

} // namespace pfd
