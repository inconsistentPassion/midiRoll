#include "AudioEngine.h"
#include <cmath>
#include <algorithm>

namespace pfd {

bool AudioEngine::Initialize() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = XAudio2Create(&m_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) return false;

#ifdef _DEBUG
    XAUDIO2_DEBUG_CONFIGURATION debug{};
    debug.TraceMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
    debug.BreakMask = XAUDIO2_LOG_ERRORS;
    m_xaudio2->SetDebugConfiguration(&debug);
#endif

    hr = m_xaudio2->CreateMasteringVoice(&m_masterVoice);
    return SUCCEEDED(hr);
}

void AudioEngine::Shutdown() {
    SineVoice::CleanupDoneVoices();
    if (m_masterVoice) { m_masterVoice->DestroyVoice(); m_masterVoice = nullptr; }
    if (m_xaudio2) { m_xaudio2->Release(); m_xaudio2 = nullptr; }
    CoUninitialize();
}

// ---- SineVoice statics ----

std::vector<SineVoice*>& SineVoice::ActiveVoices() {
    static std::vector<SineVoice*> voices;
    return voices;
}

void SineVoice::CleanupDoneVoices() {
    auto& voices = ActiveVoices();
    for (auto* v : voices) {
        if (v->IsDone()) v->Destroy();
    }
    voices.erase(
        std::remove_if(voices.begin(), voices.end(),
            [](SineVoice* v) { return v->IsDone(); }),
        voices.end()
    );
}

void SineVoice::Destroy() {
    if (m_voice) {
        m_voice->Stop();
        m_voice->FlushSourceBuffers();
        m_voice->DestroyVoice();
        m_voice = nullptr;
    }
    delete this;
}

void AudioEngine::PlayBeep(int note, float duration, float volume) {
    if (!m_xaudio2) return;
    float freq = 440.0f * std::powf(2.0f, (note - 69) / 12.0f);
    SineVoice* v = new SineVoice(m_xaudio2, freq, duration, volume);
    SineVoice::ActiveVoices().push_back(v);
}

// ---- SineVoice ----

SineVoice::SineVoice(IXAudio2* xaudio2, float frequency, float duration, float volume) {
    const uint32_t sampleRate = 44100;
    uint32_t numSamples = (uint32_t)(sampleRate * duration);
    m_buffer.resize(numSamples);

    for (uint32_t i = 0; i < numSamples; i++) {
        float t = (float)i / sampleRate;
        float env = 1.0f;
        float attack = 0.005f;
        float release = 0.1f;
        if (t < attack) env = t / attack;
        else if (t > duration - release) env = (duration - t) / release;
        m_buffer[i] = std::sinf(6.28318f * frequency * t) * volume * env;
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = sampleRate;
    wfx.wBitsPerSample  = 32;
    wfx.nBlockAlign     = 4;
    wfx.nAvgBytesPerSec = sampleRate * 4;

    xaudio2->CreateSourceVoice(&m_voice, &wfx, 0, 2.0f, this);

    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = (UINT32)(m_buffer.size() * sizeof(float));
    buf.pAudioData = (const BYTE*)m_buffer.data();
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    m_voice->SubmitSourceBuffer(&buf);
    m_voice->Start(0);
}

void STDMETHODCALLTYPE SineVoice::OnBufferEnd(void*) {
    // Can't destroy voices in XAudio2 callback - defer to next frame
    m_done = true;
}

} // namespace pfd
