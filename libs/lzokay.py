# Compatibility shim for hosts where the optional native lzokay extension is
# unavailable. Keep importing it possible so the STALKER Anomaly game plugin
# can provide its mod support, but make the missing save parser explicit.

AVAILABLE = False


def decompress(_data, _size=None):
    raise RuntimeError(
        "STALKER Anomaly save parsing is disabled because native lzokay is unavailable"
    )
