#pragma once
#include <Windows.h>
#include <array>
#include <vector>

namespace pfd {

// Maps keyboard keys to MIDI notes for live testing
// Bottom two rows of QWERTY → C3 to B4 (white + black keys)
class Input {
public:
    void OnKey(int vkCode, bool down);
    bool IsKeyDown(int vkCode) const;

    // Get MIDI note from keyboard (returns -1 if not mapped)
    static int KeyToMidiNote(int vkCode);

    // For polling note-on/note-off events from keyboard or MIDI device
    struct KeyEvent {
        int  note;         // MIDI note number
        int  velocity{64}; // 1-127 for NoteOn (default 64 for keyboard), 0 for NoteOff
        bool isDown;
    };
    const std::vector<KeyEvent>& GetEvents() const { return m_events; }
    void ClearEvents() { m_events.clear(); }
    // Push a note event. Velocity defaults to 64 (keyboard has no pressure).
    void PushEvent(int note, bool isDown, int velocity = 64) {
        m_events.push_back({note, velocity, isDown});
    }

private:
    std::array<bool, 256> m_keys{};
    std::vector<KeyEvent> m_events;
};

} // namespace pfd
