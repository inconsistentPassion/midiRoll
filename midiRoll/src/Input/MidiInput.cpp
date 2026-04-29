#include "MidiInput.h"
#pragma comment(lib, "winmm.lib")

namespace pfd {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

int MidiInput::DeviceCount() {
    return static_cast<int>(midiInGetNumDevs());
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool MidiInput::Open(int deviceIndex) {
    Close();

    UINT numDevs = midiInGetNumDevs();
    if (numDevs == 0 || deviceIndex < 0 || deviceIndex >= static_cast<int>(numDevs))
        return false;

    // Get device name
    MIDIINCAPSW caps{};
    if (midiInGetDevCapsW(static_cast<UINT>(deviceIndex), &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
        int len = WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            m_deviceName.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, m_deviceName.data(), len, nullptr, nullptr);
        }
    }

    MMRESULT res = midiInOpen(&m_hMidi,
                              static_cast<UINT>(deviceIndex),
                              reinterpret_cast<DWORD_PTR>(&MidiProc),
                              reinterpret_cast<DWORD_PTR>(this),
                              CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) {
        m_hMidi = nullptr;
        m_deviceName.clear();
        m_deviceIndex = -1;
        return false;
    }

    m_deviceIndex = deviceIndex;
    midiInStart(m_hMidi);
    return true;
}

bool MidiInput::Reopen() {
    if (m_deviceIndex < 0) return false;
    int idx = m_deviceIndex;
    return Open(idx);
}

void MidiInput::Close() {
    if (m_hMidi) {
        midiInStop(m_hMidi);
        midiInReset(m_hMidi);
        midiInClose(m_hMidi);
        m_hMidi = nullptr;
    }
    m_deviceName.clear();
    m_deviceIndex = -1;
}

// ---------------------------------------------------------------------------
// Poll (game thread)
// ---------------------------------------------------------------------------

std::vector<MidiInput::NoteEvent> MidiInput::Poll() {
    std::vector<NoteEvent> out;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        out.swap(m_pending);
    }
    return out;
}

// ---------------------------------------------------------------------------
// MIDI callback (Windows MM thread — keep it short, no allocs if possible)
// ---------------------------------------------------------------------------

/*static*/ void CALLBACK MidiInput::MidiProc(HMIDIIN /*hMidi*/, UINT msg,
                                               DWORD_PTR instance,
                                               DWORD_PTR param1, DWORD_PTR /*param2*/) {
    if (msg != MIM_DATA) return;
    auto* self = reinterpret_cast<MidiInput*>(instance);
    self->HandleMessage(static_cast<DWORD>(param1));
}

void MidiInput::HandleMessage(DWORD raw) {
    // MIDI short message layout: [status | data1 | data2 | 0]
    BYTE status   = raw & 0xFF;
    BYTE data1    = (raw >> 8)  & 0xFF;
    BYTE data2    = (raw >> 16) & 0xFF;
    BYTE command  = status & 0xF0;
    int  channel  = status & 0x0F;  // preserve MIDI channel 0-15

    std::lock_guard<std::mutex> lk(m_mutex);

    if (command == 0x90 && data2 > 0) {
        m_pending.push_back({NoteEvent::Kind::NoteOn,  channel, data1, data2, true});
    } else if (command == 0x80 || (command == 0x90 && data2 == 0)) {
        m_pending.push_back({NoteEvent::Kind::NoteOff, channel, data1, 0,     false});
    } else if (command == 0xB0) {
        // Control Change: sustain (64), sostenuto (66), soft pedal (67),
        // expression (11), volume (7), pan (10), modulation (1), etc.
        m_pending.push_back({NoteEvent::Kind::ControlChange,    channel, data1, data2, false});
    } else if (command == 0xE0) {
        // Pitch Bend: 14-bit value, LSB=data1, MSB=data2 → range 0-16383, center 8192
        int bend = data1 | (data2 << 7);
        m_pending.push_back({NoteEvent::Kind::PitchBend,        channel, 0,     bend,  false});
    } else if (command == 0xC0) {
        m_pending.push_back({NoteEvent::Kind::ProgramChange,    channel, data1, 0,     false});
    } else if (command == 0xD0) {
        // Channel Pressure (aftertouch) — single pressure value for entire channel
        m_pending.push_back({NoteEvent::Kind::ChannelPressure,  channel, data1, 0,     false});
    } else if (command == 0xA0) {
        // Key Pressure (polyphonic aftertouch) — per-note pressure
        m_pending.push_back({NoteEvent::Kind::KeyPressure,      channel, data1, data2, false});
    }
    // SysEx (0xF0) ignored for live input.
}

} // namespace pfd
