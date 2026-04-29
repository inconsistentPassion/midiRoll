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

    // Handle existing note on same channel to prevent leaks
    uint32_t key = MakeNoteKey(note, channel);
    auto it = m_visualNoteIndex.find(key);
    if (it != m_visualNoteIndex.end()) {
        size_t oldIdx = it->second;
        if (oldIdx < m_visualNotes.size() && m_visualNotes[oldIdx].active) {
            m_visualNotes[oldIdx].active = false;
            m_visualNotes[oldIdx].offTime = time;
        }
    }

    // Add new visual note
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
        m_keyState[i].active = false;
        m_keyState[i].offTime = time;
    }
    for (auto& vn : m_visualNotes) {
        if (vn.active) {
            vn.active = false;
            vn.offTime = time;
        }
    }
}

void NoteState::UpdateVisualNotes(double currentTime) {
    // Remove notes that have moved completely off-screen
    // Also rebuild the hash map to maintain consistency
    size_t writeIdx = 0;
    m_visualNoteIndex.clear();
    
    for (size_t readIdx = 0; readIdx < m_visualNotes.size(); ++readIdx) {
        auto& vn = m_visualNotes[readIdx];
        
        // Keep active notes or recently deactivated ones (within 6 seconds)
        if (vn.active || (currentTime - vn.offTime <= 6.0)) {
            if (readIdx != writeIdx) {
                m_visualNotes[writeIdx] = std::move(vn);
            }
            
            // Rebuild hash map entry for this note
            // IMPORTANT: use the object at writeIdx because vn might have been moved!
            const auto& noteObj = m_visualNotes[writeIdx];
            uint32_t key = MakeNoteKey(noteObj.note, noteObj.channel);
            
            // If multiple instances of the same note exist (e.g., one active, one finishing),
            // ensure the map points to the active one so NoteOff can find it.
            if (noteObj.active || m_visualNoteIndex.find(key) == m_visualNoteIndex.end()) {
                m_visualNoteIndex[key] = writeIdx;
            }
            
            writeIdx++;
        }
    }
    
    m_visualNotes.resize(writeIdx);
}

void NoteState::ClearVisualNotes() {
    m_visualNotes.clear();
    m_visualNoteIndex.clear();
}

} // namespace pfd
