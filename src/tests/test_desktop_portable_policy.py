#!/usr/bin/env python3

"""Regression checks for desktop integration in portable and direct launches."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


def arguments_at_top_level(text):
    """Split C++ call arguments, retaining commas inside nested expressions."""
    depth = 0
    quote = None
    escaped = False
    start = 0
    result = []
    for index, character in enumerate(text):
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
        elif character in ("'", '"'):
            quote = character
        elif character in "({[":
            depth += 1
        elif character in ")}]":
            depth -= 1
        elif character == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    result.append(text[start:].strip())
    return result


class DesktopPortablePolicyTests(unittest.TestCase):
    def test_directory_choosers_retain_native_directory_semantics(self):
        found = 0
        for path in (ROOT / "src/src").glob("*.cpp"):
            if path.name == "filedialogmemory.cpp":
                continue
            for match in re.finditer(
                r"QFileDialog::getExistingDirectory\s*\((.*?)\);",
                path.read_text(encoding="utf-8"),
                flags=re.DOTALL,
            ):
                found += 1
                arguments = arguments_at_top_level(match.group(1))
                # Omitting options uses Qt's ShowDirsOnly default. An explicit
                # empty options object disables that native-directory contract.
                if len(arguments) >= 4:
                    self.assertIn("QFileDialog::ShowDirsOnly", arguments[3], path)
                    self.assertNotIn("DontUseNativeDialog", arguments[3], path)
        self.assertGreater(found, 0)

    def test_portal_is_packaged_and_selected_before_qapplication(self):
        packaging = (ROOT / "docker/build-inner.sh").read_text(encoding="utf-8")
        self.assertIn("platformthemes/libqxdgdesktopportal.so", packaging)
        self.assertIn('if [ ! -f "${PORTAL_THEME_PLUGIN}" ]; then', packaging)
        self.assertIn("export QT_QPA_PLATFORMTHEME=xdgdesktopportal", packaging)

        main = (ROOT / "src/src/main.cpp").read_text(encoding="utf-8")
        before_application = main.split("MOApplication app(argc, argv);", 1)[0]
        self.assertIn("selectNativeDialogPlatformTheme();", before_application)
        application = (ROOT / "src/src/moapplication.cpp").read_text(encoding="utf-8")
        constructor = application.split(
            "MOApplication::MOApplication(int& argc, char** argv)", 1
        )[1].split('TimeThis const tt("MOApplication()");', 1)[0]
        self.assertIn("FLUORINE_ORIG_QT_QPA_PLATFORMTHEME", constructor)
        self.assertIn('qunsetenv("QT_QPA_PLATFORMTHEME")', constructor)
        self.assertIn('qputenv("QT_QPA_PLATFORMTHEME", original)', constructor)

    def test_fontconfig_configuration_remains_host_owned(self):
        for relative in (
            "src/src/main.cpp", "src/src/env.cpp", "src/src/nxmhandler_linux.cpp"
        ):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn('"FONTCONFIG_FILE"', source)
            self.assertNotIn('"FONTCONFIG_PATH"', source)
        packaging = (ROOT / "docker/build-inner.sh").read_text(encoding="utf-8")
        self.assertIn('SKIP_PATTERN="${SKIP_PATTERN}|libfontconfig\\.so"', packaging)
        self.assertNotIn("export FONTCONFIG_FILE=", packaging)
        self.assertNotIn("export FONTCONFIG_PATH=", packaging)

    def test_upgrade_retires_stale_fontconfig_without_removing_other_files(self):
        packaging = (ROOT / "docker/build-inner.sh").read_text(encoding="utf-8")
        cleanup = packaging.split(
            "# Retire previously bundled host libraries after an overlay update.", 1
        )[1].split("# Refresh the manifest", 1)[0]
        with tempfile.TemporaryDirectory(prefix="fluorine desktop ") as directory:
            library = Path(directory) / "lib"
            library.mkdir()
            for name in ("libfontconfig.so.1.12.0", "libcustom.so", "user-notes.txt"):
                (library / name).write_text("preserve unrelated data", encoding="utf-8")
            (library / "libfontconfig.so.1").symlink_to("libfontconfig.so.1.12.0")
            environment = dict(os.environ, BIN_DST=directory)
            subprocess.run(["bash", "-eu", "-c", cleanup], env=environment, check=True)
            self.assertEqual(
                sorted(path.name for path in library.iterdir()),
                ["libcustom.so", "user-notes.txt"],
            )


if __name__ == "__main__":
    unittest.main()
