#include "NoteState.h"
#include <algorithm>

namespace pfd {

void NoteState::NoteOn(int note, int velocity, int channel, double time) {
    if (note < 0 || note >= NOTE_COUNT) return;

    // Update key state (for piano rendering)
    m_keyState[note].activeChannels |= (1ULL << (channel % 64));
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

    int ch = channel % 64;
    bool sustainDown = (m_sustainActive >> ch) & 1;

    // Always end the visual note bar at key-release time.
    // Sustain pedal only keeps the piano key lit — it does NOT extend the note bar.
    uint32_t key = MakeNoteKey(note, channel);
    auto it = m_visualNoteIndex.find(key);
    if (it != m_visualNoteIndex.end()) {
        size_t index = it->second;
        if (index < m_visualNotes.size() && m_visualNotes[index].active) {
            m_visualNotes[index].active = false;
            m_visualNotes[index].offTime = time;
        }
    }

    if (sustainDown) {
        // Key physically released but sustain pedal holds the audio/key highlight.
        m_keyState[note].sustainChannels |= (1ULL << ch);
        return;
    }

    // Update key state
    m_keyState[note].activeChannels &= ~(1ULL << ch);
    m_keyState[note].sustainChannels &= ~(1ULL << ch);
    if (m_keyState[note].activeChannels == 0 && m_keyState[note].sustainChannels == 0) {
        m_keyState[note].active = false;
        m_keyState[note].offTime = time;
    }
}

void NoteState::SustainOn(int channel) {
    int ch = channel % 64;
    m_sustainActive |= (1ULL << ch);
}

void NoteState::SustainOff(int channel, double time) {
    int ch = channel % 64;
    m_sustainActive &= ~(1ULL << ch);

    // Release all notes that were being held by sustain on this channel
    for (int note = 0; note < NOTE_COUNT; note++) {
        if (!((m_keyState[note].sustainChannels >> ch) & 1)) continue;

        m_keyState[note].sustainChannels &= ~(1ULL << ch);
        m_keyState[note].activeChannels  &= ~(1ULL << ch);

        if (m_keyState[note].activeChannels == 0 && m_keyState[note].sustainChannels == 0) {
            m_keyState[note].active = false;
            m_keyState[note].offTime = time;
        }

        uint32_t key = MakeNoteKey(note, channel);
        auto it = m_visualNoteIndex.find(key);
        if (it != m_visualNoteIndex.end()) {
            size_t index = it->second;
            if (index < m_visualNotes.size() && m_visualNotes[index].active) {
                m_visualNotes[index].active = false;
                m_visualNotes[index].offTime = time;
            }
        }
    }
}

void NoteState::AllNotesOff(double time) {
    m_sustainActive = 0;
    for (int i = 0; i < NOTE_COUNT; i++) {
        m_keyState[i].active = false;
        m_keyState[i].activeChannels = 0;
        m_keyState[i].sustainChannels = 0;
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
