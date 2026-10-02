#pragma once
#include <windows.h>
#include <cstdint>

namespace fo4audio {
constexpr std::uint32_t runtime163 = 0x010A0A30;
constexpr std::size_t sourceSize = 0xE0;
constexpr std::uintptr_t sourceVtableRva = 0x2E09DE0;
constexpr std::uintptr_t durationRva = 0x1AF5DB0;
constexpr std::uintptr_t loopDurationRva = 0x1AF5E40;
constexpr std::uintptr_t loadStatusRva = 0x1AEEAF0;

struct Interface {
    std::uint32_t f4seVersion, runtimeVersion, editorVersion, isEditor;
};
struct PluginInfo {
    std::uint32_t infoVersion;
    const char* name;
    std::uint32_t version;
};
bool install(std::uint8_t* image);
void initializeLog(HMODULE module);
void log(const char* message);
}
