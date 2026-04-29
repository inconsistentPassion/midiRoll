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

    // Add new visual note with O(1) hash map lookup
    uint32_t key = MakeNoteKey(note, channel);
    size_t index = m_visualNotes.size();
    m_visualNotes.push_back({note, channel, velocity, time, 0, true});
    m_visualNoteIndex[key] = index;
    
    m_recentOns.push_back(note);
}

void NoteState::NoteOff(int note, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;

    // Update key state
    if (m_keyState[note].channel == channel) {
        m_keyState[note].active = false;
        m_keyState[note].offTime = time;
    }

    // O(1) lookup using hash map instead of linear search
    uint32_t key = MakeNoteKey(note, channel);
    auto it = m_visualNoteIndex.find(key);
    if (it != m_visualNoteIndex.end()) {
        size_t index = it->second;
        if (index < m_visualNotes.size() && m_visualNotes[index].active) {
            m_visualNotes[index].active = false;
            m_visualNotes[index].offTime = time;
        }
        // Don't erase from map - we'll clean up in UpdateVisualNotes
    }
}

void NoteState::AllNotesOff(double time) {
    for (int i = 0; i < NOTE_COUNT; i++) {
        if (m_keyState[i].active) NoteOff(i, m_keyState[i].channel, time);
    }
}

void NoteState::UpdateVisualNotes(double currentTime) {
    // Remove notes that have moved completely off-screen
    // Also rebuild the hash map to maintain consistency
    size_t writeIdx = 0;
    m_visualNoteIndex.clear();
    
    for (size_t readIdx = 0; readIdx < m_visualNotes.size(); ++readIdx) {
        const auto& vn = m_visualNotes[readIdx];
        
        // Keep active notes or recently deactivated ones (within 6 seconds)
        if (vn.active || (currentTime - vn.offTime <= 6.0)) {
            if (readIdx != writeIdx) {
                m_visualNotes[writeIdx] = std::move(m_visualNotes[readIdx]);
            }
            
            // Rebuild hash map entry for this note
            uint32_t key = MakeNoteKey(vn.note, vn.channel);
            m_visualNoteIndex[key] = writeIdx;
            writeIdx++;
        }
    }
    
    m_visualNotes.resize(writeIdx);
}

} // namespace pfd
