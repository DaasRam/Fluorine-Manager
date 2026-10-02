#!/usr/bin/env python3
"""Run the Fallout audio guard against a locally supplied private game image.

Requires an already unpacked 1.10.163 image for code inspection. This never
starts the game, changes the game installation, or uses its Wine prefix.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def windows(path):
    return "Z:" + str(path.resolve()).replace("/", "\\")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("bundle", "image", "proton", "runtime", "steam", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compat = out / "compatdata"
    compat.mkdir()
    for name in ("fo4-audio-guard-test.exe", "FluorineAudioGuard.dll"):
        shutil.copy2(args.bundle / name, out / name)
    (out / "FluorineAudioGuard.sources.tsv").write_text(
        "00000001:00766177:00000002\tsound\\test.wav [loose: test fixture]\n")
    env = os.environ.copy()
    for key in ("WINEPREFIX", "WINEDLLOVERRIDES", "WINEARCH", "WINESERVER", "WINELOADER"):
        env.pop(key, None)
    env.update(STEAM_COMPAT_CLIENT_INSTALL_PATH=str(args.steam.resolve()),
               STEAM_COMPAT_DATA_PATH=str(compat), SteamAppId="0", SteamGameId="0",
               STEAM_COMPAT_APP_ID="0", UMU_ID="umu-default", PROTON_USE_XALIA="0",
               PROTON_LOG="1", PROTON_LOG_DIR=str(out), WINEDEBUG="-all",
               FLUORINE_AUDIO_LOG_DIR=windows(out / "failures"))
    failed = False
    for mode in ("before", "after"):
        report = out / f"{mode}.txt"
        command = [str(args.runtime.resolve() / "run"), "--", str(args.proton.resolve() / "proton"),
                   "waitforexitandrun", str(out / "fo4-audio-guard-test.exe"), windows(args.image),
                   mode, windows(report)]
        with (out / f"{mode}-launcher.log").open("w") as log:
            try:
                result = subprocess.run(command, env=env, cwd=out, stdout=log,
                                        stderr=subprocess.STDOUT, timeout=120)
                rc = result.returncode
            except subprocess.TimeoutExpired:
                rc = "timeout"
        text = report.read_text(errors="replace") if report.exists() else ""
        passed = rc == 0 and "PASS" in text and "FAIL" not in text
        if mode == "after":
            logs = list((out / "failures").glob("*.log"))
            passed &= len(logs) == 1
            if logs:
                diagnostics = logs[0].read_text(errors="replace")
                passed &= all(token in diagnostics for token in (
                    "install=ok runtime=1.10.163", "install=refused:",
                    "operation=sound-load-failed", "operation=duration-suppressed",
                    "operation=loop-duration-suppressed", "resource_id=00000001:00766177:00000002",
                    "sound\\test.wav [loose: test fixture]", "source_flags=0x3000015"))
                passed &= diagnostics.count("=== FluorineAudioGuard ") == diagnostics.count("=== end record ===")
        print(f"{'PASS' if passed else 'FAIL'} {mode}: exit={rc}\n{text}", flush=True)
        failed |= not passed
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
