#!/usr/bin/env bash
set -euo pipefail

uid="${FLUORINE_BUILD_HOST_UID:-}"
gid="${FLUORINE_BUILD_HOST_GID:-}"
if [[ ! "${uid}" =~ ^[0-9]+$ || ! "${gid}" =~ ^[0-9]+$ ]]; then
    exit 0
fi

restore_tree() {
    local tree="$1"
    # Do not follow symlinks, or cross into filesystems mounted below either
    # bind mount. This covers the complete CMake build and persistent ccache.
    if [[ -d "${tree}" && ! -L "${tree}" ]]; then
        if ! find -P "${tree}" -xdev \
            \( ! -uid "${uid}" -o ! -gid "${gid}" \) \
            -exec chown --no-dereference "${uid}:${gid}" {} +; then
            echo "WARNING: Could not restore host ownership for ${tree}" >&2
        fi
    fi
}

restore_tree /src/build
restore_tree /ccache
