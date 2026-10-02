#!/usr/bin/env python3
"""Run bundled audio probes with one Proton, using only an isolated prefix.

Pass that Proton's Steam Linux Runtime explicitly (sniper for GE 10,
SteamLinuxRuntime_4 for GE 11). Repeat for each runner in the release matrix.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--proton", required=True, type=Path)
    parser.add_argument("--runtime", required=True, type=Path)
    parser.add_argument("--steam", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="New directory for the disposable prefix and logs")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    compat = args.output / "compatdata"
    compat.mkdir()
    env = os.environ.copy()
    env.update(STEAM_COMPAT_CLIENT_INSTALL_PATH=str(args.steam.resolve()),
               STEAM_COMPAT_DATA_PATH=str(compat), SteamAppId="0", SteamGameId="0",
               STEAM_COMPAT_APP_ID="0", UMU_ID="umu-default", PROTON_USE_XALIA="0",
               PROTON_LOG="1", PROTON_LOG_DIR=str(args.output), WINEDEBUG="-all")
    # Probe processes must not inherit an application's prefix or overrides.
    for key in ("WINEPREFIX", "WINEDLLOVERRIDES", "WINEARCH", "WINESERVER", "WINELOADER"):
        env.pop(key, None)
    failed = False
    for variant in ("safe", "latest"):
        for arch, pe in (("i686", "i386-windows"), ("x86_64", "x86_64-windows")):
            stage = args.output / variant / arch
            shutil.copytree(args.bundle / variant / pe, stage)
            names = sorted(path.stem for path in stage.glob("*.dll"))
            env["WINEDLLOVERRIDES"] = ",".join(names) + "=n"
            probes = {"wma": "PASS WMA worker threads", "smoke": "PASS 35 modules;"}
            if variant == "latest":
                probes["invalid-buffer"] = "PASS invalid voice lifecycle;"
                probes["invalid-default"] = "PASS invalid voice lifecycle;"
                probes["invalid-winehome"] = "PASS invalid voice lifecycle;"
            for probe, expected in probes.items():
                probe_env = env.copy()
                failure_dir = stage / "failures" / probe
                probe_env["FLUORINE_AUDIO_LOG_DIR"] = "Z:" + str(failure_dir).replace("/", "\\")
                exe = stage / f"{probe}.exe"
                binary = "invalid-buffer" if probe.startswith("invalid-") else probe
                shutil.copy2(args.bundle / "tests" / f"{arch}-{binary}.exe", exe)
                report = stage / f"{probe}.txt"
                report_windows = "Z:" + str(report).replace("/", "\\")
                command = [str(args.runtime.resolve() / "run"), "--",
                           str(args.proton.resolve() / "proton"), "waitforexitandrun", str(exe)]
                if probe == "smoke":
                    command.append("register")
                command.append(report_windows)
                if probe == "invalid-default":
                    probe_env.pop("FLUORINE_AUDIO_LOG_DIR", None)
                    probe_env["XDG_DATA_HOME"] = str(failure_dir)
                    failure_dir = failure_dir / "fluorine/logs/audio"
                elif probe == "invalid-winehome":
                    command.append("\\??\\Z:" + str(failure_dir).replace("/", "\\"))
                    failure_dir = failure_dir / ".local/share/fluorine/logs/audio"
                with (stage / f"{probe}-launcher.log").open("w") as log:
                    try:
                        result = subprocess.run(command, env=probe_env, stdout=log,
                                                stderr=subprocess.STDOUT, timeout=120)
                        rc = result.returncode
                    except subprocess.TimeoutExpired:
                        rc = "timeout"
                text = report.read_text(errors="replace") if report.exists() else ""
                passed = rc == 0 and expected in text
                if variant == "latest":
                    errors = sorted(failure_dir.glob("*.log"))
                    if probe.startswith("invalid-"):
                        # 10 formats, one sample-rate failure, 32 concurrent
                        # failures and one buffer rejection, in each API DLL.
                        passed &= len(errors) == 2
                        for error in errors:
                            records = error.read_text(errors="replace")
                            passed &= records.count("=== FAudio failure ") == 44
                            passed &= records.count("=== end failure ===") == 44
                            passed &= all(token in records for token in (
                                "function=FAudio_CreateSourceVoice", "tag=0x0 channels=0 rate=0 align=0",
                                "function=FAudioSourceVoice_SubmitSourceBuffer", "context=", "12345678",
                                "call_stack:", f"{probe}.exe+0x", "asset_filename=unavailable"))
                    else:
                        passed &= not errors
                print(f"{'PASS' if passed else 'FAIL'} {variant} {arch} {probe}: exit={rc}", flush=True)
                failed |= not passed
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
