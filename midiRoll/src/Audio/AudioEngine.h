#pragma once
#include <xaudio2.h>
#include <cstdint>
#include <vector>

namespace pfd {

class AudioEngine {
public:
    bool Initialize();
    void Shutdown();

    // Play a simple sine wave beep (for testing without SoundFont)
    void PlayBeep(int note, float duration = 0.5f, float volume = 0.3f);

    // Get master voice for spectrum analysis
    IXAudio2MasteringVoice* MasterVoice() const { return m_masterVoice; }
    IXAudio2*               XAudio2() const { return m_xaudio2; }

private:
    IXAudio2*              m_xaudio2{};
    IXAudio2MasteringVoice* m_masterVoice{};
};

// Simple sine wave voice for testing
class SineVoice : public IXAudio2VoiceCallback {
public:
    SineVoice(IXAudio2* xaudio2, float frequency, float duration, float volume);

    void STDMETHODCALLTYPE OnStreamEnd() {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) {}
    void STDMETHODCALLTYPE OnBufferEnd(void* ctx);
    void STDMETHODCALLTYPE OnBufferStart(void*) {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) {}

    bool IsDone() const { return m_done; }
    void Destroy(); // Call outside XAudio2 callback

    // Track active voices for deferred cleanup
    static std::vector<SineVoice*>& ActiveVoices();
    static void CleanupDoneVoices();

private:
    IXAudio2SourceVoice* m_voice{};
    std::vector<float>   m_buffer;
    bool                 m_done = false;
};

} // namespace pfd
