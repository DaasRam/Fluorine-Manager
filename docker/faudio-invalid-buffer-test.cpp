// Exercise malformed voice creation and the running audio thread through
// Fallout 4's XAudio2 2.7 interface and the same implementation in XAudio2 2.9.
#include <windows.h>
#include <xaudio2.h>
#include <cstdio>
#include <initializer_list>

static DWORD WINAPI concurrentFailures(void *argument)
{
    auto voice = static_cast<IXAudio2SourceVoice*>(argument);
    for (unsigned i=0; i<16; ++i) {
        if (voice->SetSourceSampleRate(0)!=XAUDIO2_E_INVALID_CALL) return 1;
    }
    return 0;
}

// The SDK exposes the modern engine interface. These legacy methods precede
// CreateSourceVoice in 2.7; source SubmitSourceBuffer/DestroyVoice have the same
// ABI in both versions. Do not use the modern GetVoiceDetails/GetState on 2.7.
struct LegacyAudio : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCount(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceDetails(UINT32, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Initialize(UINT32, UINT32) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterForCallbacks(IXAudio2EngineCallback*) = 0;
    virtual void STDMETHODCALLTYPE UnregisterForCallbacks(IXAudio2EngineCallback*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSourceVoice(IXAudio2SourceVoice**,
        const WAVEFORMATEX*, UINT32, float, IXAudio2VoiceCallback*,
        const XAUDIO2_VOICE_SENDS*, const XAUDIO2_EFFECT_CHAIN*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateSubmixVoice(IXAudio2SubmixVoice**,
        UINT32, UINT32, UINT32, UINT32, const XAUDIO2_VOICE_SENDS*,
        const XAUDIO2_EFFECT_CHAIN*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateMasteringVoice(IXAudio2MasteringVoice**,
        UINT32, UINT32, UINT32, UINT32, const XAUDIO2_EFFECT_CHAIN*) = 0;
    virtual HRESULT STDMETHODCALLTYPE StartEngine() = 0;
    virtual void STDMETHODCALLTYPE StopEngine() = 0;
    virtual HRESULT STDMETHODCALLTYPE CommitChanges(UINT32) = 0;
    virtual void STDMETHODCALLTYPE GetPerformanceData(void*) = 0;
    virtual void STDMETHODCALLTYPE SetDebugConfiguration(const XAUDIO2_DEBUG_CONFIGURATION*, void*) = 0;
};

struct Callback : IXAudio2VoiceCallback {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT error = S_OK;
    ~Callback() { CloseHandle(done); }
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnStreamEnd() override { SetEvent(done); }
    void STDMETHODCALLTYPE OnBufferStart(void*) override {}
    void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT hr) override { error=hr; SetEvent(done); }
};

static bool testModule(unsigned version)
{
    char name[32], path[4096];
    std::snprintf(name, sizeof(name), "xaudio2_%u.dll", version);
    HMODULE module = LoadLibraryA(name);
    if (!module) return false;
    GetModuleFileNameA(module, path, sizeof(path));
    std::printf("LOADED %s\n", path);
    LegacyAudio *legacy = nullptr;
    IXAudio2 *modern = nullptr;
    IXAudio2MasteringVoice *master = nullptr;
    if (version == 7) {
        const CLSID clsid = {0x5a508685, 0xa254, 0x4fba,
            {0x9b, 0x82, 0x9a, 0x24, 0xb0, 0x03, 0x06, 0xaf}};
        const IID iid = {0x8bcf1f58, 0x9fe7, 0x4583,
            {0x8a, 0xc6, 0xe2, 0xad, 0xc4, 0x65, 0xc8, 0xbb}};
        auto getClass = reinterpret_cast<HRESULT (WINAPI *)(REFCLSID, REFIID, void**)>(
            GetProcAddress(module, "DllGetClassObject"));
        IClassFactory *factory = nullptr;
        if (!getClass || FAILED(getClass(clsid, IID_IClassFactory,
                                        reinterpret_cast<void**>(&factory)))) return false;
        HRESULT hr = factory->CreateInstance(nullptr, iid, reinterpret_cast<void**>(&legacy));
        factory->Release();
        if (FAILED(hr) || FAILED(legacy->Initialize(0, 0xffffffff)) ||
            FAILED(legacy->CreateMasteringVoice(&master, 2, 48000, 0, 0, nullptr))) return false;
    } else {
        auto create = reinterpret_cast<HRESULT (WINAPI *)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR)>(
            GetProcAddress(module, "XAudio2Create"));
        if (!create || FAILED(create(&modern, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
            FAILED(modern->CreateMasteringVoice(&master, 2, 48000))) return false;
    }
    // The game can turn off debug output. Durable failure logging must survive.
    XAUDIO2_DEBUG_CONFIGURATION quiet={};
    if (legacy) legacy->SetDebugConfiguration(&quiet, nullptr);
    else modern->SetDebugConfiguration(&quiet, nullptr);
    Callback callback;
    auto createVoice = [&](IXAudio2SourceVoice **voice, const WAVEFORMATEX &format) {
        return legacy ? legacy->CreateSourceVoice(voice, &format, 0, 1.0f, &callback, nullptr, nullptr)
                      : modern->CreateSourceVoice(voice, &format, 0, 1.0f, &callback);
    };
    const WAVEFORMATEX pcm = {WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0};
    IXAudio2SourceVoice *voice = nullptr;
    for (unsigned fault=0; fault<10; ++fault) {
        WAVEFORMATEX format = pcm;
        if (fault==0) format={};
        if (fault==1) format.nBlockAlign=0;
        if (fault==2) format.nChannels=0;
        if (fault==3) format.nSamplesPerSec=0;
        if (fault==4) format.wBitsPerSample=0;
        if (fault==5) format.wFormatTag=0;
        if (fault==6) format.wFormatTag=0xffff;
        if (fault==7) format.nBlockAlign=3;
        if (fault==8) format={WAVE_FORMAT_ADPCM,1,44100,0,6,4,0};
        if (fault==9) format.wFormatTag=WAVE_FORMAT_EXTENSIBLE; // Truncated extension.
        voice=nullptr;
        HRESULT hr=createVoice(&voice,format);
        std::printf("XAudio2 2.%u invalid format %u: %08lx voice=%p\n",
            version,fault,static_cast<unsigned long>(hr),voice);
        if (hr!=XAUDIO2_E_INVALID_CALL || voice!=nullptr) return false;
    }
    for (WORD bits : {8,16,24,32}) {
        WAVEFORMATEX format=pcm;
        format.wBitsPerSample=bits;
        format.nBlockAlign=bits/8;
        format.nAvgBytesPerSec=format.nBlockAlign*format.nSamplesPerSec;
        if (FAILED(createVoice(&voice,format))) return false;
        voice->DestroyVoice();
    }
    for (WORD bits : {16,32}) {
        WAVEFORMATEX format=pcm;
        format.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;
        format.wBitsPerSample=bits;
        format.nBlockAlign=bits/8;
        format.nAvgBytesPerSec=format.nBlockAlign*format.nSamplesPerSec;
        if (FAILED(createVoice(&voice,format))) return false;
        voice->DestroyVoice();
    }
    WAVEFORMATEXTENSIBLE extensible={};
    extensible.Format=pcm;
    extensible.Format.wFormatTag=WAVE_FORMAT_EXTENSIBLE;
    extensible.Format.cbSize=22;
    extensible.Samples.wValidBitsPerSample=16;
    extensible.SubFormat={1,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    if (FAILED(createVoice(&voice,extensible.Format))) return false;
    voice->DestroyVoice();
    // FAudio intentionally computes ADPCM samples per block from alignment,
    // even when the caller did not supply an ADPCMWAVEFORMAT extension.
    WAVEFORMATEX adpcm={WAVE_FORMAT_ADPCM,1,44100,0,7,4,0};
    if (FAILED(createVoice(&voice,adpcm))) return false;
    voice->DestroyVoice();

    // Exercise the background thread, including a started voice with no data.
    // Submission-only tests missed the second Fallout 4 divide-by-zero path.
    if (FAILED(createVoice(&voice, pcm))) return false;
    if (voice->SetSourceSampleRate(0)!=XAUDIO2_E_INVALID_CALL) return false;
    HANDLE workers[2] = {CreateThread(nullptr,0,concurrentFailures,voice,0,nullptr),
                         CreateThread(nullptr,0,concurrentFailures,voice,0,nullptr)};
    for (HANDLE worker : workers) {
        DWORD result=1;
        if (!worker || WaitForSingleObject(worker,5000)!=WAIT_OBJECT_0 ||
            !GetExitCodeThread(worker,&result) || result!=0) return false;
        CloseHandle(worker);
    }
    XAUDIO2_BUFFER invalid={};
    invalid.LoopBegin=1;
    invalid.pContext=reinterpret_cast<void*>(0x12345678);
    if (voice->SubmitSourceBuffer(&invalid)!=XAUDIO2_E_INVALID_CALL) return false;
    if (FAILED(voice->Start())) return false;
    Sleep(50);
    XAUDIO2_BUFFER buffer = {};
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) return false;
    Sleep(50);
    short samples[4410] = {};
    buffer.Flags=XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = sizeof(samples);
    buffer.pAudioData = reinterpret_cast<const BYTE*>(samples);
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) return false;
    if (WaitForSingleObject(callback.done,5000)!=WAIT_OBJECT_0 || FAILED(callback.error)) return false;
    voice->DestroyVoice();
    master->DestroyVoice();
    if (legacy) legacy->Release();
    if (modern) modern->Release();
    FreeLibrary(module);
    return true;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !std::freopen(argv[1], "w", stdout)) return 1;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Exercise older Wine's NT home path without touching the user's logs.
    if (argc>2) {
        SetEnvironmentVariableA("FLUORINE_AUDIO_LOG_DIR", nullptr);
        SetEnvironmentVariableA("WINE_HOST_XDG_DATA_HOME", nullptr);
        SetEnvironmentVariableA("XDG_DATA_HOME", nullptr);
        SetEnvironmentVariableA("WINE_HOST_HOME", nullptr);
        SetEnvironmentVariableA("HOME", nullptr);
        SetEnvironmentVariableA("WINEHOMEDIR", argv[2]);
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    if (!testModule(7) || !testModule(9)) return 3;
    CoUninitialize();
    std::puts("PASS invalid voice lifecycle; XAudio2 2.7 and 2.9; background playback completed");
    return 0;
}
