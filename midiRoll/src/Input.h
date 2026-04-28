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

    // For polling note-on/note-off events from keyboard
    struct KeyEvent {
        int  note;     // MIDI note number
        bool isDown;
    };
    const std::vector<KeyEvent>& GetEvents() const { return m_events; }
    void ClearEvents() { m_events.clear(); }

private:
    std::array<bool, 256> m_keys{};
    std::vector<KeyEvent> m_events;
};

} // namespace pfd
