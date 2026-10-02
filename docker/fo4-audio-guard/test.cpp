// Integration probe: read an owned, unpacked 1.10.163 image without running its
// entry point. Exercise the actual audio leaf functions with synthetic objects.
// The proprietary image is supplied locally; it is never part of this repo.
#include "guard.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
std::uint8_t* image;
FILE* report;
using Duration = std::uint32_t (*)(void*);
using Status = bool (*)(void*, bool);
template<class T> void put(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::uint8_t*>(object) + offset, &value, sizeof(value));
}
template<class T> T get(void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<std::uint8_t*>(object) + offset, sizeof(value));
    return value;
}
void check(bool pass, const char* description) {
    if (!pass) {
        std::fprintf(report, "FAIL %s\n", description);
        std::fflush(report);
        std::exit(1);
    }
}
LONG CALLBACK division(EXCEPTION_POINTERS* error) {
    auto rva = reinterpret_cast<std::uintptr_t>(error->ExceptionRecord->ExceptionAddress) -
               reinterpret_cast<std::uintptr_t>(image);
    bool expected = error->ExceptionRecord->ExceptionCode == EXCEPTION_INT_DIVIDE_BY_ZERO && rva == 0x1AF5E06;
    std::fprintf(report, "%s original EXCEPTION_INT_DIVIDE_BY_ZERO at Fallout4.exe+0x%llx\n",
                 expected ? "PASS" : "FAIL", static_cast<unsigned long long>(rva));
    std::fflush(report);
    ExitProcess(expected ? 0 : 1);
}

std::uint8_t* mapImage(const char* filename) {
    auto file = std::fopen(filename, "rb");
    check(file != nullptr, "open private game image");
    std::fseek(file, 0, SEEK_END);
    auto length = std::ftell(file);
    std::rewind(file);
    check(length > 4096 && length < 200000000, "image size");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    check(std::fread(data.data(), 1, data.size(), file) == data.size(), "read image");
    std::fclose(file);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(data.data());
    check(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 1024, "DOS header");
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(data.data() + dos->e_lfanew);
    check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.SizeOfImage > 0x2E09E00 &&
          nt->OptionalHeader.SizeOfImage < 200000000, "PE header");
    auto mapped = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    check(mapped != nullptr, "allocate private image");
    check(nt->OptionalHeader.SizeOfHeaders <= data.size(), "header bounds");
    std::memcpy(mapped, data.data(), nt->OptionalHeader.SizeOfHeaders);
    auto sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        auto& s = sections[i];
        check(std::uint64_t(s.PointerToRawData) + s.SizeOfRawData <= data.size() &&
              std::uint64_t(s.VirtualAddress) + s.SizeOfRawData <= nt->OptionalHeader.SizeOfImage, "section bounds");
        std::memcpy(mapped + s.VirtualAddress, data.data() + s.PointerToRawData, s.SizeOfRawData);
    }
    // These leaf functions use only object fields and RIP-relative constants.
    // Relocate the four vtable entries used by the guard; no imports, TLS,
    // entry point or other game initialization is executed by this probe.
    auto table = reinterpret_cast<std::uintptr_t*>(mapped + fo4audio::sourceVtableRva);
    for (unsigned i = 0; i < 4; ++i)
        table[i] += reinterpret_cast<std::uintptr_t>(mapped) - nt->OptionalHeader.ImageBase;
    return mapped;
}

using Source = std::array<std::uint8_t, fo4audio::sourceSize>;
Source pcm() {
    Source s{};
    put(s.data(), 0, image + fo4audio::sourceVtableRva);
    put<std::uint32_t>(s.data(), 0x0C, 0x03000015);
    put<std::uint16_t>(s.data(), 0x20, 1);
    put<std::uint16_t>(s.data(), 0x22, 2);
    put<std::uint32_t>(s.data(), 0x24, 48000);
    put<std::uint32_t>(s.data(), 0x28, 192000);
    put<std::uint16_t>(s.data(), 0x2C, 4);
    put<std::uint16_t>(s.data(), 0x2E, 16);
    put<std::uint32_t>(s.data(), 0x7C, 192000);
    put<std::uint32_t>(s.data(), 0x90, 9);
    put<std::uint32_t>(s.data(), 0x8C, 24000);
    return s;
}
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    report = std::fopen(argv[3], "wb");
    if (!report) return 2;
    image = mapImage(argv[1]);
    auto original = reinterpret_cast<Duration>(image + fo4audio::durationRva);
    auto originalLoop = reinterpret_cast<Duration>(image + fo4audio::loopDurationRva);
    auto status = reinterpret_cast<Status>(image + fo4audio::loadStatusRva);
    auto table = reinterpret_cast<Duration*>(image + fo4audio::sourceVtableRva);
    auto s = pcm();
    check(original(s.data()) == 1000 && originalLoop(s.data()) == 501, "known PCM duration/loop fixture");
    if (!std::strcmp(argv[2], "before")) {
        put<std::uint16_t>(s.data(), 0x22, 0);
        put<std::uint32_t>(s.data(), 0x24, 1070141419);
        put<std::uint16_t>(s.data(), 0x2C, 0);
        put<std::uint16_t>(s.data(), 0x2E, 0);
        AddVectoredExceptionHandler(1, division);
        original(s.data());
        check(false, "baseline must reproduce reported division");
    }
    fo4audio::initializeLog(GetModuleHandleW(nullptr));
    // Refuse foreign hooks without changing anything else.
    auto first = image[fo4audio::loadStatusRva];
    image[fo4audio::loadStatusRva] = 0xCC;
    check(!fo4audio::install(image) && table[2] == original && table[3] == originalLoop,
          "conflicting hook refused without partial changes");
    image[fo4audio::loadStatusRva] = first;
    check(fo4audio::install(image), "install guard against original executable signatures");
    check(!fo4audio::install(image), "duplicate installation refused");
    auto guarded = table[2], guardedLoop = table[3];
    for (std::uint16_t channels : {1, 2, 6, 8}) for (std::uint16_t bits : {8, 16, 24, 32}) {
        s = pcm();
        put(s.data(), 0x22, channels);
        put(s.data(), 0x2E, bits);
        put<std::uint16_t>(s.data(), 0x2C, channels * (bits / 8));
        auto copy = s;
        check(guarded(s.data()) == original(s.data()) && guardedLoop(s.data()) == originalLoop(s.data()) && s == copy,
              "valid PCM preserves original calculations and source");
    }
    std::uint32_t packets[] = {96000, 192000};
    s = pcm();
    put<std::uint16_t>(s.data(), 0x20, 0x161);
    put<std::uint16_t>(s.data(), 0x2C, 2048);
    put<std::uint32_t>(s.data(), 0x84, 2);
    put(s.data(), 0xC0, packets);
    check(guarded(s.data()) == 1000 && guardedLoop(s.data()) == originalLoop(s.data()), "valid WMA packet duration");
    put<std::uint32_t>(s.data(), 0x84, 0);
    check(guarded(s.data()) == 0 && guardedLoop(s.data()) == 0, "zero WMA packet count");
    put<std::uint32_t>(s.data(), 0x84, 2);
    put<std::uintptr_t>(s.data(), 0xC0, 1);
    check(guarded(s.data()) == 0 && guardedLoop(s.data()) == 0, "unreadable WMA packet table");
    for (unsigned kind = 0; kind < 6; ++kind) {
        s = pcm();
        if (kind == 0 || kind == 5) put<std::uint16_t>(s.data(), 0x2C, 0);
        if (kind == 1 || kind == 5) put<std::uint16_t>(s.data(), 0x22, 0);
        if (kind == 2 || kind == 5) put<std::uint32_t>(s.data(), 0x24, 1070141419);
        if (kind == 3 || kind == 5) put<std::uint16_t>(s.data(), 0x2E, 0);
        if (kind == 4) put<std::uint16_t>(s.data(), 0x2C, 1);
        auto copy = s;
        check(guarded(s.data()) == 0 && guardedLoop(s.data()) == 0 && s == copy, "invalid duration suppressed without changing format");
        std::array<std::uint8_t, 0xA0> sound{};
        put(sound.data(), 0x68, s.data());
        put<std::uint32_t>(sound.data(), 0x48, 1);
        put<std::uint32_t>(sound.data(), 0x4C, 0x00766177);
        put<std::uint32_t>(sound.data(), 0x50, 2);
        put<std::uint32_t>(sound.data(), 0x98, 0x81FF);
        check(!status(sound.data(), false), "failed load result preserved");
        put<std::uint32_t>(copy.data(), 0x0C, 0x15);
        check(s == copy, "only invalid source readiness bits cleared");
        check(get<std::uint32_t>(sound.data(), 0x98) == ((0x81FF & ~9u) | 0x404), "original game failure flags preserved");
    }
    s = pcm();
    auto copy = s;
    std::array<std::uint8_t, 0xA0> sound{};
    put(sound.data(), 0x68, s.data());
    check(status(sound.data(), true) && s == copy, "success does not alter source");
    check(!status(sound.data(), false) && s == copy, "unrelated failure preserves valid shared source");
    put<std::uintptr_t>(sound.data(), 0x68, 0);
    check(!status(sound.data(), false), "missing source failure");
    auto plugin = LoadLibraryA("FluorineAudioGuard.dll");
    check(plugin != nullptr, "DLL loads without external C++ runtime");
    using Query = bool (*)(const fo4audio::Interface*, fo4audio::PluginInfo*);
    auto exportAddress = GetProcAddress(plugin, "F4SEPlugin_Query");
    Query query;
    static_assert(sizeof(query) == sizeof(exportAddress));
    std::memcpy(&query, &exportAddress, sizeof(query));
    check(query != nullptr, "F4SE query export");
    fo4audio::Interface runtime{0, fo4audio::runtime163, 0, 0};
    fo4audio::PluginInfo info{};
    check(query(&runtime, &info) && info.infoVersion == 1, "1.10.163 accepted");
    runtime.runtimeVersion += 16;
    check(!query(&runtime, &info), "other game versions refused");
    runtime.runtimeVersion = fo4audio::runtime163;
    runtime.isEditor = 1;
    check(!query(&runtime, &info), "editor refused");
    std::fprintf(report, "PASS actual Fallout 4 leaf functions: original crash reproduced separately; guarded PCM/WMA, failure flags, hook conflicts, DLL ABI and version gates\n");
    std::fclose(report);
    return 0;
}
