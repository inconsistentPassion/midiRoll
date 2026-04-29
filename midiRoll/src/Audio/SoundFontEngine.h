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
    bool                  m_initialized{};
    int                   m_sfontId{-1};
    bool                  m_soundFontLoaded{};
    std::string           m_soundFontPath;
    float                 m_volume{0.7f};

    std::mutex             m_eventMutex;
    std::queue<AudioEvent> m_eventQueue;
};

} // namespace pfd
