#include "SoundFontEngine.h"
#include <algorithm>
#include <thread>

#if HAS_FLUIDSYNTH
// === Full FluidSynth implementation ===
namespace pfd {

bool SoundFontEngine::Initialize() {
    m_settings = new_fluid_settings();
    if (!m_settings) return false;

    // --- Audio Driver ---
    fluid_settings_setstr(m_settings, "audio.driver", "wasapi");

    // --- Sample Rate: 44100 Hz (MuseScore default) ---
    fluid_settings_setnum(m_settings, "synth.sample-rate", 44100.0);

    // --- Buffer size: 1024 samples x 4 periods = extra headroom against
    //     underruns (choppy/static audio) when many voices are active ---
    fluid_settings_setint(m_settings, "audio.period-size", 1024);
    fluid_settings_setint(m_settings, "audio.periods", 4);

    // --- MIDI Channels ---
    fluid_settings_setint(m_settings, "synth.midi-channels", 64);

    // --- Parallelize per-voice DSP rendering across CPU cores.
    //     This is the primary fix for choppy/static audio when many notes
    //     play at once: a single core can fall behind the audio callback's
    //     deadline once polyphony gets high (chords, sustain, dense MIDI +
    //  reverb/chorus), causing buffer underruns that sound like
    //     crackling/static. Spreading voice synthesis across cores keeps
    //     each callback deadline comfortably met. ---
    unsigned int cores = std::thread::hardware_concurrency();
    fluid_settings_setint(m_settings, "synth.cpu-cores", (int)std::clamp(cores, 1u, 8u));

    // --- Polyphony: generous ceiling so busy passages steal voices
    //     gracefully (quick fade) instead of relying on hard cutoffs ---
    fluid_settings_setint(m_settings, "synth.polyphony", 256);

    m_synth = new_fluid_synth(m_settings);
    if (!m_synth) { delete_fluid_settings(m_settings); m_settings = nullptr; return false; }
    fluid_synth_set_gain(m_synth, m_volume);

    // --- Reverb: MuseScore Studio defaults ---
    // Small room size + high level = subtle, present shimmer (not boomy)
    fluid_synth_set_reverb_group_roomsize(m_synth, -1, 0.2);
    fluid_synth_set_reverb_group_damp(m_synth, -1, 0.0);
    fluid_synth_set_reverb_group_width(m_synth, -1, 0.5);
    fluid_synth_set_reverb_group_level(m_synth, -1, 0.9);
    fluid_synth_reverb_on(m_synth, -1, 1);

    // --- Chorus: MuseScore Studio defaults ---
    // 3 voices, sine wave, adds warm stereo shimmer
    fluid_synth_chorus_on(m_synth, -1, 1);
    fluid_synth_set_chorus_group_nr(m_synth, -1, 3);
    fluid_synth_set_chorus_group_level(m_synth, -1, 2.0);
    fluid_synth_set_chorus_group_speed(m_synth, -1, 0.3);
    fluid_synth_set_chorus_group_depth(m_synth, -1, 8.0);
    fluid_synth_set_chorus_group_type(m_synth, -1, FLUID_CHORUS_MOD_SINE);

    m_audioDriver = new_fluid_audio_driver(m_settings, m_synth);

    // --- Initialize high-resolution sequencer ---
    m_sequencer = new_fluid_sequencer2(0);
    if (m_sequencer) {
        m_synthSeqId = fluid_sequencer_register_fluidsynth(m_sequencer, m_synth);
    }

    m_initialized = true;
    return true;
}

void SoundFontEngine::Shutdown() {
    if (m_sequencer) { delete_fluid_sequencer(m_sequencer); m_sequencer = nullptr; m_synthSeqId = -1; }
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

uint32_t SoundFontEngine::GetSequencerTick() const {
    if (!m_sequencer) return 0;
    return (uint32_t)fluid_sequencer_get_tick(m_sequencer);
}

void SoundFontEngine::ClearScheduledEvents() {
    if (!m_sequencer) return;
    fluid_sequencer_remove_events(m_sequencer, -1, -1, -1);
}

void SoundFontEngine::ConfigureReverb(double roomsize, double damping, double width, double level) {
    if (!m_synth) return;
    fluid_synth_set_reverb_group_roomsize(m_synth, -1, roomsize);
    fluid_synth_set_reverb_group_damp(m_synth, -1, damping);
    fluid_synth_set_reverb_group_width(m_synth, -1, width);
    fluid_synth_set_reverb_group_level(m_synth, -1, level);
    fluid_synth_reverb_on(m_synth, -1, 1);
}

void SoundFontEngine::ConfigureChorus(int voices, double level, double speed, double depth) {
    if (!m_synth) return;
    fluid_synth_chorus_on(m_synth, -1, 1);
    fluid_synth_set_chorus_group_nr(m_synth, -1, voices);
    fluid_synth_set_chorus_group_level(m_synth, -1, level);
    fluid_synth_set_chorus_group_speed(m_synth, -1, speed);
    fluid_synth_set_chorus_group_depth(m_synth, -1, depth);
    fluid_synth_set_chorus_group_type(m_synth, -1, FLUID_CHORUS_MOD_SINE);
}

void SoundFontEngine::ScheduleNoteOn(int ch, int note, int vel, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_noteon(ev, ch, (short)note, (short)vel);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::ScheduleNoteOff(int ch, int note, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_noteoff(ev, ch, (short)note);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::ScheduleControlChange(int ch, int ctrl, int val, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_control_change(ev, ch, (short)ctrl, val);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::SchedulePitchBend(int ch, int val, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_pitch_bend(ev, ch, val);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::ScheduleProgramChange(int ch, int prog, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_program_change(ev, ch, prog);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::ScheduleChannelPressure(int ch, int val, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_channel_pressure(ev, ch, val);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
}

void SoundFontEngine::ScheduleKeyPressure(int ch, int note, int val, uint32_t tick) {
    if (!m_sequencer || m_synthSeqId < 0) return;
    fluid_event_t* ev = new_fluid_event();
    if (!ev) return;
    fluid_event_set_source(ev, -1);
    fluid_event_set_dest(ev, m_synthSeqId);
    fluid_event_key_pressure(ev, ch, (short)note, val);
    fluid_sequencer_send_at(m_sequencer, ev, tick, 1);
    delete_fluid_event(ev);
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

uint32_t SoundFontEngine::GetSequencerTick() const { return 0; }
void SoundFontEngine::ClearScheduledEvents() {}
void SoundFontEngine::ScheduleNoteOn(int, int, int, uint32_t) {}
void SoundFontEngine::ScheduleNoteOff(int, int, uint32_t) {}
void SoundFontEngine::ScheduleControlChange(int, int, int, uint32_t) {}
void SoundFontEngine::SchedulePitchBend(int, int, uint32_t) {}
void SoundFontEngine::ScheduleProgramChange(int, int, uint32_t) {}
void SoundFontEngine::ScheduleChannelPressure(int, int, uint32_t) {}
void SoundFontEngine::ScheduleKeyPressure(int, int, int, uint32_t) {}
void SoundFontEngine::ConfigureReverb(double, double, double, double) {}
void SoundFontEngine::ConfigureChorus(int, double, double, double) {}

} // namespace pfd
#endif
