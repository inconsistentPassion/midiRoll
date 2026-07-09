#pragma once
#include <string>
#include <mutex>
#include <queue>
#include <cstdint>

#if __has_include(<fluidsynth.h>)
#define HAS_FLUIDSYNTH 1
#include <fluidsynth.h>
#else
#define HAS_FLUIDSYNTH 0
struct fluid_settings_t;
struct fluid_synth_t;
struct fluid_audio_driver_t;
struct fluid_sequencer_t;
typedef int fluid_seq_id_t;
#endif

namespace pfd {

class SoundFontEngine {
public:
    bool Initialize();
    void Shutdown();

    bool LoadSoundFont(const std::string& path);
    bool IsSoundFontLoaded() const { return m_soundFontLoaded; }
    const std::string& GetSoundFontPath() const { return m_soundFontPath; }
    bool HasFluidSynth() const { return HAS_FLUIDSYNTH; }

    void NoteOn(int channel, int note, int velocity);
    void NoteOff(int channel, int note);
    void ControlChange(int channel, int control, int value);
    void PitchBend(int channel, int value);
    void ProgramChange(int channel, int program);
    void ChannelPressure(int channel, int value);
    void KeyPressure(int channel, int note, int value);
    void AllNotesOff();

    void SetVolume(float vol);
    float GetVolume() const { return m_volume; }

    void ProcessEvents();

    // Sequencer-based scheduling methods
    uint32_t GetSequencerTick() const;
    void ClearScheduledEvents();
    void ScheduleNoteOn(int channel, int note, int velocity, uint32_t tick);
    void ScheduleNoteOff(int channel, int note, uint32_t tick);
    void ScheduleControlChange(int channel, int control, int value, uint32_t tick);
    void SchedulePitchBend(int channel, int value, uint32_t tick);
    void ScheduleProgramChange(int channel, int program, uint32_t tick);
    void ScheduleChannelPressure(int channel, int value, uint32_t tick);
    void ScheduleKeyPressure(int channel, int note, int value, uint32_t tick);

    // Reverb and chorus configuration
    void ConfigureReverb(double roomsize, double damping, double width, double level);
    void ConfigureChorus(int voices, double level, double speed, double depth);

private:
    struct AudioEvent {
        enum class Type { 
            NoteOn, NoteOff, AllNotesOff, Volume, 
            ControlChange, PitchBend, ProgramChange, ChannelPressure, KeyPressure 
        } type;
        int   channel{};
        int   data1{}; // note / control / program / pitch low
        int   data2{}; // velocity / value / pitch high
        float volume{};
    };

    fluid_settings_t*     m_settings{};
    fluid_synth_t*        m_synth{};
    fluid_audio_driver_t* m_audioDriver{};
    fluid_sequencer_t*    m_sequencer{};
    fluid_seq_id_t        m_synthSeqId{-1};
    bool                  m_initialized{};
    int                   m_sfontId{-1};
    bool                  m_soundFontLoaded{};
    std::string           m_soundFontPath;
    float                 m_volume{0.7f};

    std::mutex             m_eventMutex;
    std::queue<AudioEvent> m_eventQueue;
};

} // namespace pfd
