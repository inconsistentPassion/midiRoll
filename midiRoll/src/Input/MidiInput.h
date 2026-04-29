#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace pfd {

// Receives live MIDI input from the first available MIDI-in device.
// Thread-safe: the MIDI callback runs on a separate Windows MM thread;
// the game thread calls Poll() each frame to drain queued events.
class MidiInput {
public:
    struct NoteEvent {
        enum class Kind {
            NoteOn,
            NoteOff,
            ControlChange,
            PitchBend,
            ProgramChange,
            ChannelPressure, // 0xD0 — aftertouch for whole channel
            KeyPressure,     // 0xA0 — polyphonic aftertouch per note
        };
        Kind    kind{Kind::NoteOn};
        int     channel{};  // MIDI channel 0-15
        int     data1{};    // note / CC number / program / key (KeyPressure)
        int     data2{};    // velocity / CC value / pitch bend / pressure value
        bool    isDown{};   // true = NoteOn, false = NoteOff (Note kinds only)
    };

    MidiInput() = default;
    ~MidiInput() { Close(); }

    // Open the device at `deviceIndex`. Returns true on success.
    bool Open(int deviceIndex = 0);
    void Close();

    // Close and reopen the same device index. Returns true on success.
    bool Reopen();

    bool IsOpen() const { return m_hMidi != nullptr; }

    // Index of the currently open device, or -1 if none.
    int DeviceIndex() const { return m_deviceIndex; }

    // Name of the currently open device, or "" if none.
    const std::string& DeviceName() const { return m_deviceName; }

    // Number of MIDI-in devices present on the system.
    static int DeviceCount();

    // Drain all events accumulated since the last Poll().
    // Call once per frame from the game thread.
    std::vector<NoteEvent> Poll();

private:
    HMIDIIN     m_hMidi{};
    std::string m_deviceName;
    int         m_deviceIndex{-1};

    std::mutex             m_mutex;
    std::vector<NoteEvent> m_pending;

    // Windows MIDI callback (MM thread)
    static void CALLBACK MidiProc(HMIDIIN hMidi, UINT msg,
                                   DWORD_PTR instance,
                                   DWORD_PTR param1, DWORD_PTR param2);
    void HandleMessage(DWORD msg);
};

} // namespace pfd
