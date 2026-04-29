#include "NoteState.h"
#include <algorithm>

namespace pfd {

void NoteState::NoteOn(int note, int velocity, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;

    // Update key state (for piano rendering)
    m_keyState[note].active = true;
    m_keyState[note].velocity = (uint8_t)velocity;
    m_keyState[note].channel = (uint8_t)channel;
    m_keyState[note].onTime = time;
    m_keyState[note].visualActive = true;

    // Add new visual note (for trail rendering)
    m_visualNotes.push_back({note, channel, velocity, time, 0, true});
    m_recentOns.push_back(note);
}

void NoteState::NoteOff(int note, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;

    // Update key state
    if (m_keyState[note].channel == channel) {
        m_keyState[note].active = false;
        m_keyState[note].offTime = time;
    }

    // Update the matching visual note(s)
    for (auto& vn : m_visualNotes) {
        if (vn.note == note && vn.channel == channel && vn.active) {
            vn.active = false;
            vn.offTime = time;
        }
    }
}

void NoteState::AllNotesOff(double time) {
    for (int i = 0; i < NOTE_COUNT; i++) {
        if (m_keyState[i].active) NoteOff(i, m_keyState[i].channel, time);
    }
}

void NoteState::UpdateVisualNotes(double currentTime) {
    // Remove notes that have moved completely off-screen (e.g., 5 seconds past off-time)
    m_visualNotes.erase(
        std::remove_if(m_visualNotes.begin(), m_visualNotes.end(),
            [&](const ActiveVisualNote& vn) {
                return !vn.active && (currentTime - vn.offTime > 6.0);
            }),
        m_visualNotes.end()
    );
}

} // namespace pfd
