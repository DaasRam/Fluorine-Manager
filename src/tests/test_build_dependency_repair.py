import importlib.util
import os
import pathlib
import shlex
import subprocess
import tempfile
import unittest

script = pathlib.Path(__file__).resolve().parents[2] / "docker/repair-ninja-deps.py"
spec = importlib.util.spec_from_file_location("repair_ninja_deps", script)
repair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repair)
ownership_helper = pathlib.Path(__file__).resolve().parents[2] / "docker/build-owner-env.sh"


class DependencyRepairTests(unittest.TestCase):
    def test_discards_only_objects_with_empty_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name in ("stale.o", "valid.o", "notes.txt"):
                (root / name).write_text("keep unless stale")
            log = ("stale.o: #deps 0, deps mtime 123 (VALID)\n"
                   "valid.o: #deps 2, deps mtime 456 (VALID)\n"
                   "    source.cpp\n    header.h\n"
                   "notes.txt: #deps 0, deps mtime 123 (VALID)\n")
            self.assertEqual(repair.repair_objects(root, log), ["stale.o"])
            self.assertFalse((root / "stale.o").exists())
            self.assertTrue((root / "valid.o").exists())
            self.assertTrue((root / "notes.txt").exists())
            self.assertEqual(repair.repair_objects(root, log), [])

    def test_keeps_files_outside_build_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "build").mkdir()
            (root / "external.o").write_text("keep")
            with self.assertRaises(ValueError):
                repair.repair_objects(root / "build", "../external.o: #deps 0, invalid")
            self.assertTrue((root / "external.o").exists())


class BuildOwnershipDetectionTests(unittest.TestCase):
    def _run_engine(self, engine: str, *, output: str = "", fail: bool = False) -> tuple[str, str]:
        with tempfile.TemporaryDirectory() as directory:
            fake_bin = pathlib.Path(directory)
            fake_engine = fake_bin / engine
            fake_engine.write_text(
                """#!/usr/bin/env bash
if [[ "$1" != info ]]; then exit 2; fi
if [[ "${MOCK_INFO_FAIL:-}" == 1 ]]; then exit 3; fi
printf '%s\n' "${MOCK_INFO_OUTPUT:-}"
"""
            )
            fake_engine.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = f"{fake_bin}:{env['PATH']}"
            env["MOCK_INFO_OUTPUT"] = output
            if fail:
                env["MOCK_INFO_FAIL"] = "1"
            else:
                env.pop("MOCK_INFO_FAIL", None)
            # The helper's Docker template differs from Podman's; the mock
            # returns the same configured response for either info query.
            command = f"""source {shlex.quote(str(ownership_helper))}
fluorine_set_build_host_ownership {shlex.quote(engine)}
printf '%s\n%s\n' "${{FLUORINE_BUILD_HOST_UID}}" "${{FLUORINE_BUILD_HOST_GID}}"
"""
            result = subprocess.run(
                ["bash", "-c", command], env=env, check=True,
                capture_output=True, text=True,
            )
            uid, gid = result.stdout.splitlines()
            return uid, gid

    def test_docker_only_passes_ids_for_confirmed_rootful_engine(self):
        host_ids = (str(os.getuid()), str(os.getgid()))
        self.assertEqual(self._run_engine("docker", output='["name=seccomp"]'), host_ids)
        self.assertEqual(self._run_engine("docker", output='["name=rootless"]'), ("", ""))
        self.assertEqual(self._run_engine("docker", output='["name=userns"]'), ("", ""))
        self.assertEqual(self._run_engine("docker", fail=True), ("", ""))

    def test_podman_keeps_existing_rootless_detection(self):
        host_ids = (str(os.getuid()), str(os.getgid()))
        self.assertEqual(self._run_engine("podman", output="false"), host_ids)
        self.assertEqual(self._run_engine("podman", output="true"), ("", ""))
        self.assertEqual(self._run_engine("podman", fail=True), ("", ""))


if __name__ == "__main__":
    unittest.main()
