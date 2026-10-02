#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: build.sh OUTPUT_DIRECTORY}"
mkdir -p "${out}"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -static-libgcc -static-libstdc++ -shared "${root}/guard.cpp" \
    -o "${out}/FluorineAudioGuard.dll"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -static-libgcc -static-libstdc++ -DFO4_AUDIO_GUARD_TEST \
    "${root}/guard.cpp" "${root}/test.cpp" -o "${out}/fo4-audio-guard-test.exe"
