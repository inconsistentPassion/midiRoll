#include "NoteState.h"

namespace pfd {

static constexpr int blacks[] = {1, 3, 6, 8, 10};

bool NoteState::IsBlackKey(int note) {
    int m = note % 12;
    for (int b : blacks) if (m == b) return true;
    return false;
}

void NoteState::NoteOn(int note, int velocity, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;
    auto& n = m_notes[note];
    n.active = true;
    n.visualActive = true;
    n.velocity = (uint8_t)velocity;
    n.channel = (uint8_t)channel;
    n.onTime = time;
    m_recentOns.push_back(note);
}

void NoteState::NoteOff(int note, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;
    auto& n = m_notes[note];
    n.active = false;
    n.offTime = time;
    m_recentOffs.push_back(note);
}

void NoteState::AllNotesOff(double time) {
    for (int i = 0; i < NOTE_COUNT; i++) {
        if (m_notes[i].active) {
            NoteOff(i, 0, time);
        }
    }
}

} // namespace pfd
