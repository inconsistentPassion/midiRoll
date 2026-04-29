#include "Input.h"

namespace pfd {

// QWERTY bottom row: A S D F G H J K L ;  → C3 Eb3 F3 G3 Bb3 C4 Eb4 F4 G4 Bb4
// QWERTY top row:    W E   T Y U   O P  → C#3 D#3  F#3 G#3 A#3  C#4 D#4  F#4 G#4
// (piano-style layout on keyboard)

static struct KeyMap { int vk; int note; } s_keyMap[] = {
    // Bottom row (white keys)
    {'A', 60}, // C4
    {'S', 62}, // D4
    {'D', 64}, // E4
    {'F', 65}, // F4
    {'G', 67}, // G4
    {'H', 69}, // A4
    {'J', 71}, // B4
    {'K', 72}, // C5
    {'L', 74}, // D5
    {0xBA, 76}, // ; → E5

    // Top row (black keys)
    {'W', 61}, // C#4
    {'E', 63}, // D#4
    {'T', 66}, // F#4
    {'Y', 68}, // G#4
    {'U', 70}, // A#4
    {'O', 73}, // C#5
    {'P', 75}, // D#5

    // Z-M row (lower octave)
    {'Z', 48}, // C3
    {'X', 50}, // D3
    {'C', 52}, // E3
    {'V', 53}, // F3
    {'B', 55}, // G3
    {'N', 57}, // A3
    {'M', 59}, // B3
};

int Input::KeyToMidiNote(int vkCode) {
    for (auto& km : s_keyMap) {
        if (km.vk == vkCode) return km.note;
    }
    return -1;
}

void Input::OnKey(int vkCode, bool down) {
    if (vkCode < 0 || vkCode >= 256) return;
    if (m_keys[vkCode] == down) return; // no change
    m_keys[vkCode] = down;

    int note = KeyToMidiNote(vkCode);
    if (note >= 0) {
        PushEvent(note, down);
    }
}

bool Input::IsKeyDown(int vkCode) const {
    return (vkCode >= 0 && vkCode < 256) ? m_keys[vkCode] : false;
}

} // namespace pfd
