#include "SoundFontEngine.h"
#include <algorithm>

#if HAS_FLUIDSYNTH
// === Full FluidSynth implementation ===
namespace pfd {

bool SoundFontEngine::Initialize() {
    m_settings = new_fluid_settings();
    if (!m_settings) return false;
    fluid_settings_setstr(m_settings, "audio.driver", "wasapi");
    fluid_settings_setint(m_settings, "audio.period-size", 512);
    m_synth = new_fluid_synth(m_settings);
    if (!m_synth) { delete_fluid_settings(m_settings); m_settings = nullptr; return false; }
    fluid_synth_set_gain(m_synth, m_volume);
    m_audioDriver = new_fluid_audio_driver(m_settings, m_synth);
    m_initialized = true;
    return true;
}

void SoundFontEngine::Shutdown() {
    if (m_audioDriver) { delete_fluid_audio_driver(m_audioDriver); m_audioDriver = nullptr; }
    if (m_synth) { delete_fluid_synth(m_synth); m_synth = nullptr; }
    if (m_settings) { delete_fluid_settings(m_settings); m_settings = nullptr; }
    m_initialized = false;
    m_soundFontLoaded = false;
}

bool SoundFontEngine::LoadSoundFont(const std::string& path) {
    if (!m_synth) return false;
    // Unload previous SoundFont if loaded
    if (m_soundFontLoaded && m_sfontId >= 0) {
        fluid_synth_sfunload(m_synth, m_sfontId, 1);
        m_sfontId = -1;
    }
    m_sfontId = fluid_synth_sfload(m_synth, path.c_str(), 1);
    m_soundFontLoaded = (m_sfontId >= 0);
    if (m_soundFontLoaded) m_soundFontPath = path;
    return m_soundFontLoaded;
}

void SoundFontEngine::NoteOn(int ch, int note, int vel) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::NoteOn, ch, note, vel, 0});
}
void SoundFontEngine::NoteOff(int ch, int note) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::NoteOff, ch, note, 0, 0});
}
void SoundFontEngine::ControlChange(int ch, int ctrl, int val) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::ControlChange, ch, ctrl, val, 0});
}
void SoundFontEngine::PitchBend(int ch, int val) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::PitchBend, ch, val, 0, 0});
}
void SoundFontEngine::ProgramChange(int ch, int prog) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::ProgramChange, ch, prog, 0, 0});
}
void SoundFontEngine::ChannelPressure(int ch, int val) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::ChannelPressure, ch, val, 0, 0});
}
void SoundFontEngine::KeyPressure(int ch, int note, int val) {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::KeyPressure, ch, note, val, 0});
}
void SoundFontEngine::AllNotesOff() {
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::AllNotesOff, 0, 0, 0, 0});
}
void SoundFontEngine::SetVolume(float v) {
    m_volume = std::clamp(v, 0.f, 1.f);
    std::lock_guard<std::mutex> lk(m_eventMutex);
    m_eventQueue.push({AudioEvent::Type::Volume, 0, 0, 0, m_volume});
}

void SoundFontEngine::ProcessEvents() {
    if (!m_synth) return;
    std::queue<AudioEvent> q;
    { std::lock_guard<std::mutex> lk(m_eventMutex); std::swap(q, m_eventQueue); }
    while (!q.empty()) {
        auto& e = q.front();
        switch (e.type) {
        case AudioEvent::Type::NoteOn:  fluid_synth_noteon(m_synth, e.channel, e.data1, e.data2); break;
        case AudioEvent::Type::NoteOff: fluid_synth_noteoff(m_synth, e.channel, e.data1); break;
        case AudioEvent::Type::ControlChange: fluid_synth_cc(m_synth, e.channel, e.data1, e.data2); break;
        case AudioEvent::Type::PitchBend:     fluid_synth_pitch_bend(m_synth, e.channel, e.data1); break;
        case AudioEvent::Type::ProgramChange: fluid_synth_program_change(m_synth, e.channel, e.data1); break;
        case AudioEvent::Type::ChannelPressure: fluid_synth_channel_pressure(m_synth, e.channel, e.data1); break;
        case AudioEvent::Type::KeyPressure:     fluid_synth_key_pressure(m_synth, e.channel, e.data1, e.data2); break;
        case AudioEvent::Type::AllNotesOff:   fluid_synth_all_sounds_off(m_synth, -1); break;
        case AudioEvent::Type::Volume:        fluid_synth_set_gain(m_synth, e.volume); break;
        }
        q.pop();
    }
}
} // namespace pfd

#else
// === Fallback stub when FluidSynth not available ===
namespace pfd {

bool SoundFontEngine::Initialize() {
    m_initialized = false;
    return false;
}

void SoundFontEngine::Shutdown() {
    m_initialized = false;
}

bool SoundFontEngine::LoadSoundFont(const std::string&) {
    return false;
}

void SoundFontEngine::NoteOn(int, int, int) {}
void SoundFontEngine::NoteOff(int, int) {}
void SoundFontEngine::ControlChange(int, int, int) {}
void SoundFontEngine::PitchBend(int, int) {}
void SoundFontEngine::ProgramChange(int, int) {}
void SoundFontEngine::ChannelPressure(int, int) {}
void SoundFontEngine::KeyPressure(int, int, int) {}
void SoundFontEngine::AllNotesOff() {}
void SoundFontEngine::SetVolume(float) {}
void SoundFontEngine::ProcessEvents() {}

} // namespace pfd
#endif
