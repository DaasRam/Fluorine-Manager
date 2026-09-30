#!/usr/bin/env bash

# Set host UID/GID only when the container engine confirms that root inside
# the container has the host's ordinary, unmapped root identity.
fluorine_set_build_host_ownership() {
    local engine="$1"
    local security_options rootless

    FLUORINE_BUILD_HOST_UID=""
    FLUORINE_BUILD_HOST_GID=""

    case "${engine}" in
        docker)
            # Rootless Docker and userns-remapped daemons translate container
            # UID 0 to a host/subordinate ID. Chowning to the host's numeric UID
            # from inside such a namespace would corrupt ownership instead of
            # restoring it. If daemon inspection fails, do not guess.
            if ! security_options="$(docker info --format '{{json .SecurityOptions}}' 2>/dev/null)"; then
                return 0
            fi
            if [[ ! "${security_options}" =~ ^\[.*\]$ ||
                  "${security_options}" == *rootless* ||
                  "${security_options}" == *userns* ]]; then
                return 0
            fi
            ;;
        podman)
            if ! rootless="$(podman info --format '{{.Host.Security.Rootless}}' 2>/dev/null)" ||
               [[ "${rootless}" != false ]]; then
                return 0
            fi
            ;;
        *)
            return 0
            ;;
    esac

    FLUORINE_BUILD_HOST_UID="$(id -u)"
    FLUORINE_BUILD_HOST_GID="$(id -g)"
}
