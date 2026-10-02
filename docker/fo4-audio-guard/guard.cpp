// Fallout 4 1.10.163 only. Keep game workarounds outside the generic XAudio API.
#include "guard.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace fo4audio {
namespace {
using Duration = std::uint32_t (*)(void*);
using LoadStatus = bool (*)(void*, bool);
Duration originalDuration, originalLoopDuration;
LoadStatus originalLoadStatus;
std::uint8_t* gameImage;
SRWLOCK logLock = SRWLOCK_INIT;
std::wstring logPath;
std::unordered_map<std::string, std::string> sources;

template<class T> T field(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::uint8_t*>(object) + offset, sizeof(value));
    return value;
}

bool readable(const void* address, std::size_t size) {
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    if (!cursor || cursor + size < cursor) return false;
    const auto end = cursor + size;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

const char* invalidFormat(const void* source) {
    const auto tag = field<std::uint16_t>(source, 0x20);
    const auto channels = field<std::uint16_t>(source, 0x22);
    const auto rate = field<std::uint32_t>(source, 0x24);
    const auto align = field<std::uint16_t>(source, 0x2C);
    const auto bits = field<std::uint16_t>(source, 0x2E);
    if (!channels || channels > 64) return "invalid channel count";
    if (rate < 1000 || rate > 200000) return "invalid sample rate";
    if (!align) return "zero block alignment";
    if (tag == 1 && ((bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
                     align != channels * (bits / 8))) return "invalid PCM layout";
    if (tag == 0x161) {
        if (!bits) return "zero WMA sample width";
        auto count = field<std::uint32_t>(source, 0x84);
        auto table = field<std::uintptr_t>(source, 0xC0);
        if (!count || !table || count > SIZE_MAX / sizeof(std::uint32_t) ||
            !readable(reinterpret_cast<void*>(table), std::size_t(count) * sizeof(std::uint32_t)))
            return "invalid WMA packet table";
    }
    return nullptr;
}

std::string sourceKey(const void* id) {
    char key[32];
    std::snprintf(key, sizeof(key), "%08x:%08x:%08x", field<std::uint32_t>(id, 0),
                  field<std::uint32_t>(id, 4), field<std::uint32_t>(id, 8));
    return key;
}

void logSource(const char* operation, const char* reason, const void* source,
               const void* sound, const void* caller) {
    char record[8192];
    std::string asset = "unavailable (no GameSound resource ID at this call)";
    std::string key = "unavailable";
    if (sound) {
        key = sourceKey(static_cast<const std::uint8_t*>(sound) + 0x48);
        auto found = sources.find(key);
        if (found == sources.end()) {
            auto alternate = key;
            alternate.replace(9, 8, "00000000");
            found = sources.find(alternate);
        }
        if (found != sources.end()) asset = found->second;
        else asset = "unresolved (resource ID retained for archive/loose-file lookup)";
    }
    std::snprintf(record, sizeof(record),
        "operation=%s reason=%s caller_rva=0x%llx\n"
        "sound=%p source=%p resource_id=%s\nasset_candidates=%s\n"
        "source_flags=0x%x parse_flags=0x%x audio_bytes=%u\n"
        "tag=0x%x channels=%u rate=%u bytes_per_sec=%u align=%u bits=%u extra_size=%u\n",
        operation, reason, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(caller) -
            reinterpret_cast<std::uintptr_t>(gameImage)), sound, source, key.c_str(), asset.c_str(),
        source ? field<std::uint32_t>(source, 0x0C) : 0,
        source ? field<std::uint32_t>(source, 0x90) : 0,
        source ? field<std::uint32_t>(source, 0x7C) : 0,
        source ? field<std::uint16_t>(source, 0x20) : 0,
        source ? field<std::uint16_t>(source, 0x22) : 0,
        source ? field<std::uint32_t>(source, 0x24) : 0,
        source ? field<std::uint32_t>(source, 0x28) : 0,
        source ? field<std::uint16_t>(source, 0x2C) : 0,
        source ? field<std::uint16_t>(source, 0x2E) : 0,
        source ? field<std::uint16_t>(source, 0x30) : 0);
    log(record);
}

std::uint32_t duration(void* source) {
    if (const char* reason = invalidFormat(source)) {
        logSource("duration-suppressed", reason, source, nullptr, __builtin_return_address(0));
        return 0;
    }
    return originalDuration(source);
}

std::uint32_t loopDuration(void* source) {
    if (const char* reason = invalidFormat(source)) {
        logSource("loop-duration-suppressed", reason, source, nullptr, __builtin_return_address(0));
        return 0;
    }
    return originalLoopDuration(source);
}

bool loadStatus(void* sound, bool success) {
    // The original routine updates only GameSound flags. On failure, its data
    // source can retain bits 24/25, causing ProcessSoundUpdates to call the
    // duration getters even after CreateSourceVoice returned an error.
    if (!success) {
        auto source = field<void*>(sound, 0x68);
        if (readable(source, sourceSize) &&
            field<void*>(source, 0) == gameImage + sourceVtableRva) {
            const char* reason = invalidFormat(source);
            logSource("sound-load-failed", reason ? reason : "game reported failure with a valid format",
                      source, sound, __builtin_return_address(0));
            // Do not alter a valid shared source on an unrelated voice failure.
            // Retain its storage and references: the normal destructor owns them.
            if (reason)
                InterlockedAnd(reinterpret_cast<volatile LONG*>(static_cast<std::uint8_t*>(source) + 0x0C),
                               static_cast<LONG>(~0x03000000u));
        }
    }
    return originalLoadStatus(sound, success);
}

bool mkdirs(std::wstring path) {
    std::size_t start = 3;
    if (path.rfind(L"\\\\", 0) == 0) {
        start = path.find(L'\\', 2);
        if (start == std::wstring::npos) return false;
        start = path.find(L'\\', start + 1);
        if (start == std::wstring::npos) return false;
        ++start;
    }
    for (auto i = start; i < path.size(); ++i) {
        if (path[i] != L'\\') continue;
        auto component = path.substr(0, i);
        CreateDirectoryW(component.c_str(), nullptr);
    }
    CreateDirectoryW(path.c_str(), nullptr);
    auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring directory(const wchar_t* variable, const wchar_t* suffix) {
    wchar_t value[4096];
    auto length = GetEnvironmentVariableW(variable, value, 4096);
    if (!length || length >= 4096) return {};
    std::wstring result(value);
    if (result.rfind(L"\\??\\unix\\", 0) == 0) result = L"Z:" + result.substr(8);
    else if (result.rfind(L"\\??\\", 0) == 0) result.erase(0, 4);
    if (result[0] == L'/') result = L"Z:" + result;
    if (!(result.size() > 2 && result[1] == L':' && (result[2] == L'\\' || result[2] == L'/')) &&
        result.rfind(L"\\\\", 0) != 0) return {};
    result += suffix;
    for (auto& c : result) if (c == L'/') c = L'\\';
    return result;
}

HANDLE openLog(const SYSTEMTIME& time) {
    if (!logPath.empty()) {
        auto file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) return file;
        logPath.clear();
    }
    const wchar_t* variables[] = {L"FLUORINE_AUDIO_LOG_DIR", L"WINE_HOST_XDG_DATA_HOME",
        L"XDG_DATA_HOME", L"WINE_HOST_HOME", L"HOME", L"WINEHOMEDIR", L"LOCALAPPDATA", L"TEMP"};
    const wchar_t* suffixes[] = {L"", L"/fluorine/logs/audio", L"/fluorine/logs/audio",
        L"/.local/share/fluorine/logs/audio", L"/.local/share/fluorine/logs/audio",
        L"/.local/share/fluorine/logs/audio", L"/Fluorine/logs/audio", L"/Fluorine/logs/audio"};
    wchar_t filename[160];
    std::swprintf(filename, 160, L"\\Fallout4.exe-%04u%02u%02uT%02u%02u%02u%03uZ-pid%lu-FluorineAudioGuard.log",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, GetCurrentProcessId());
    for (unsigned i = 0; i < 8; ++i) {
        auto path = directory(variables[i], suffixes[i]);
        if (path.empty() || !mkdirs(path)) continue;
        path += filename;
        auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) { logPath = path; return file; }
    }
    return INVALID_HANDLE_VALUE;
}
} // namespace

void initializeLog(HMODULE module) {
    wchar_t path[4096];
    auto length = GetModuleFileNameW(module, path, 4096);
    if (!length || length >= 4096) return;
    std::wstring manifest(path);
    manifest = manifest.substr(0, manifest.find_last_of(L"\\/")) + L"\\FluorineAudioGuard.sources.tsv";
    auto file = _wfopen(manifest.c_str(), L"rb");
    if (!file) { log("asset_manifest=unavailable; resource IDs will still be recorded\n"); return; }
    char line[8192];
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strlen(line) < 29 || line[26] != '\t') continue;
        line[26] = 0;
        auto text = std::string(line + 27);
        auto end = text.find_last_not_of("\r\n");
        if (end != std::string::npos) text.resize(end + 1);
        auto& value = sources[line];
        if (!value.empty() && value.size() < 6000) value += " | ";
        if (value.size() < 6000) value += text;
    }
    std::fclose(file);
    char message[100];
    std::snprintf(message, sizeof(message), "asset_manifest=%zu resource IDs (candidates, not load-order proof)\n", sources.size());
    log(message);
}

void log(const char* message) {
    auto saved = GetLastError();
    SYSTEMTIME time;
    GetSystemTime(&time);
    char header[160];
    std::snprintf(header, sizeof(header), "\n=== FluorineAudioGuard %04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu ===\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId());
    std::string record(header);
    record += message;
    record += "=== end record ===\n";
    OutputDebugStringA(record.c_str());
    AcquireSRWLockExclusive(&logLock);
    auto file = openLog(time);
    if (file != INVALID_HANDLE_VALUE) {
        std::size_t offset = 0;
        DWORD written;
        while (offset < record.size() && WriteFile(file, record.data() + offset,
                static_cast<DWORD>(record.size() - offset), &written, nullptr) && written)
            offset += written;
        CloseHandle(file);
        if (offset != record.size()) OutputDebugStringA("FluorineAudioGuard: incomplete log write\n");
    } else OutputDebugStringA("FluorineAudioGuard: cannot create log\n");
    ReleaseSRWLockExclusive(&logLock);
    SetLastError(saved);
}

bool install(std::uint8_t* image) {
    // This complete, position-independent leaf routine contains no relative
    // calls or RIP-relative data. Its short branches stay inside the copy.
    constexpr std::uint8_t statusCode[] = {
        0x8b,0x81,0x98,0,0,0,0x84,0xd2,0x74,0x05,0x83,0xc8,0x08,0xeb,0x03,0x83,
        0xe0,0xf7,0x83,0xe0,0xfe,0x83,0xc8,0x04,0x89,0x81,0x98,0,0,0,0x84,0xd2,
        0x75,0x0a,0x0f,0xba,0xe8,0x0a,0x89,0x81,0x98,0,0,0,0x0f,0xb6,0xc2,0xc3};
    constexpr std::uint8_t durationStart[] = {0x4c,0x8b,0xc9,0x33,0xc9,0x41,0x0f,0xb7,0x51,0x20};
    constexpr std::uint8_t loopStart[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
    if (!readable(image, sizeof(IMAGE_DOS_HEADER))) return false;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 4096) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.SizeOfImage < sourceVtableRva + 32) return false;
    auto table = reinterpret_cast<void**>(image + sourceVtableRva);
    if (!readable(table, 32) || !readable(image + loadStatusRva, sizeof(statusCode)) ||
        !readable(image + durationRva, sizeof(durationStart)) ||
        !readable(image + loopDurationRva, sizeof(loopStart)) ||
        table[2] != image + durationRva || table[3] != image + loopDurationRva ||
        std::memcmp(image + loadStatusRva, statusCode, sizeof(statusCode)) ||
        std::memcmp(image + durationRva, durationStart, sizeof(durationStart)) ||
        std::memcmp(image + loopDurationRva, loopStart, sizeof(loopStart))) {
        log("install=refused: 1.10.163 code/vtable signature mismatch or another hook is present\n");
        return false;
    }
    auto trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, sizeof(statusCode),
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!trampoline) return false;
    std::memcpy(trampoline, statusCode, sizeof(statusCode));
    DWORD protection;
    if (!VirtualProtect(trampoline, sizeof(statusCode), PAGE_EXECUTE_READ, &protection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, sizeof(statusCode));
    DWORD codeProtection, tableProtection;
    if (!VirtualProtect(image + loadStatusRva, 14, PAGE_EXECUTE_READWRITE, &codeProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    if (!VirtualProtect(table + 2, 16, PAGE_READWRITE, &tableProtection)) {
        VirtualProtect(image + loadStatusRva, 14, codeProtection, &protection);
        VirtualFree(trampoline, 0, MEM_RELEASE); return false;
    }
    gameImage = image;
    originalLoadStatus = reinterpret_cast<LoadStatus>(trampoline);
    originalDuration = reinterpret_cast<Duration>(table[2]);
    originalLoopDuration = reinterpret_cast<Duration>(table[3]);
    // F4SE Load runs before game audio threads start; never hot-patch a live game.
    std::uint8_t branch[14] = {0xff,0x25,0,0,0,0};
    auto target = reinterpret_cast<std::uintptr_t>(loadStatus);
    std::memcpy(branch + 6, &target, sizeof(target));
    std::memcpy(image + loadStatusRva, branch, sizeof(branch));
    InterlockedExchangePointer(table + 2, reinterpret_cast<void*>(duration));
    InterlockedExchangePointer(table + 3, reinterpret_cast<void*>(loopDuration));
    VirtualProtect(table + 2, 16, tableProtection, &protection);
    VirtualProtect(image + loadStatusRva, 14, codeProtection, &protection);
    FlushInstructionCache(GetCurrentProcess(), image + loadStatusRva, 14);
    log("install=ok runtime=1.10.163 version=1\n"
        "guards=failed-invalid-source-readiness,duration,loop-duration\n");
    return true;
}
} // namespace fo4audio

#ifndef FO4_AUDIO_GUARD_TEST
extern "C" __declspec(dllexport) bool F4SEPlugin_Query(const fo4audio::Interface* f4se,
                                                       fo4audio::PluginInfo* info) {
    if (!f4se || !info) return false;
    *info = {1, "FluorineAudioGuard", 1};
    return !f4se->isEditor && f4se->runtimeVersion == fo4audio::runtime163;
}

extern "C" __declspec(dllexport) bool F4SEPlugin_Load(const fo4audio::Interface* f4se) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&F4SEPlugin_Load), &module);
    fo4audio::initializeLog(module);
    if (!f4se || f4se->isEditor || f4se->runtimeVersion != fo4audio::runtime163) {
        fo4audio::log("install=refused: unsupported game version\n");
        return false;
    }
    return fo4audio::install(reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr)));
}
#endif
