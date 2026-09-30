from __future__ import annotations

import importlib.util
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).parents[2]
_WARNINGS: list[str] = []


class _QDir:
    def __init__(self, path: str):
        self._path = path

    def absolutePath(self) -> str:
        return self._path


class _Save:
    def __init__(self, filepath: Path):
        self._filepath = filepath

    def getFilepath(self) -> str:
        return self._filepath.as_posix()


class _SaveInfo:
    pass


class _BasicGame:
    def __init__(self):
        self.features: list[object] = []

    def init(self, organizer) -> bool:  # noqa: ANN001
        self._mappings = SimpleNamespace(
            savegameExtension=SimpleNamespace(get=lambda: "scop")
        )
        return True

    def _register_feature(self, feature: object) -> None:
        self.features.append(feature)


def _class(name: str, *, attrs: dict[str, object] | None = None):
    return type(name, (), attrs or {})


def _load_module(name: str, path: Path):  # noqa: ANN001, ANN201
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def _install_stubs() -> tuple[types.ModuleType, types.ModuleType]:
    pyqt = types.ModuleType("PyQt6")
    qtcore = types.ModuleType("PyQt6.QtCore")
    qtcore.QDir = _QDir
    qtcore.QFileInfo = _class("QFileInfo")
    qtcore.Qt = SimpleNamespace(
        GlobalColor=SimpleNamespace(black=0, white=1),
        WindowType=SimpleNamespace(ToolTip=1, BypassGraphicsProxyWidget=2),
        AlignmentFlag=SimpleNamespace(AlignLeft=1),
    )
    qtcore.qWarning = lambda message: _WARNINGS.append(str(message))
    qtwidgets = types.ModuleType("PyQt6.QtWidgets")
    qtwidgets.QLabel = _class("QLabel")
    qtwidgets.QVBoxLayout = _class("QVBoxLayout")
    qtwidgets.QWidget = _class("QWidget")
    pyqt.QtCore = qtcore
    pyqt.QtWidgets = qtwidgets
    sys.modules["PyQt6"] = pyqt
    sys.modules["PyQt6.QtCore"] = qtcore
    sys.modules["PyQt6.QtWidgets"] = qtwidgets

    mobase = types.ModuleType("mobase")
    mobase.IPluginFileMapper = _class("IPluginFileMapper")
    mobase.ISaveGame = _Save
    mobase.ISaveGameInfoWidget = _class("ISaveGameInfoWidget")
    mobase.SaveGameInfo = _class("SaveGameInfo")
    mobase.FileTreeEntry = _class("FileTreeEntry")
    mobase.IOrganizer = _class("IOrganizer")
    mobase.ExecutableInfo = _class("ExecutableInfo")
    mobase.Mapping = _class("Mapping")
    mobase.IFileTree = _class(
        "IFileTree",
        attrs={
            "REPLACE": 1,
            "WalkReturn": SimpleNamespace(CONTINUE=1),
        },
    )
    mobase.ModDataChecker = _class(
        "ModDataChecker",
        attrs={
            "CheckReturn": object,
            "VALID": 1,
            "FIXABLE": 2,
            "INVALID": 3,
        },
    )
    mobase.ModDataContent = _class(
        "ModDataContent", attrs={"Content": _class("Content")}
    )
    sys.modules["mobase"] = mobase

    package_name = "_stalker_save_fallback_tests"
    package = types.ModuleType(package_name)
    package.__path__ = [str(ROOT / "libs/basic_games")]
    sys.modules[package_name] = package
    games_name = f"{package_name}.games"
    games = types.ModuleType(games_name)
    games.__path__ = [str(ROOT / "libs/basic_games/games")]
    sys.modules[games_name] = games

    features_name = f"{package_name}.basic_features"
    features = types.ModuleType(features_name)
    features.__path__ = []
    sys.modules[features_name] = features
    save_info = types.ModuleType(f"{features_name}.basic_save_game_info")
    save_info.BasicGameSaveGame = _Save
    save_info.BasicGameSaveGameInfo = _SaveInfo
    sys.modules[save_info.__name__] = save_info

    basic_game = types.ModuleType(f"{package_name}.basic_game")
    basic_game.BasicGame = _BasicGame
    sys.modules[basic_game.__name__] = basic_game

    _load_module("lzokay", ROOT / "libs/lzokay.py")
    plugin = _load_module(
        f"{games_name}.game_stalkeranomaly",
        ROOT / "libs/basic_games/games/game_stalkeranomaly.py",
    )
    return plugin, mobase


GAME, MOBASE = _install_stubs()


class StalkerAnomalySaveFallbackTests(unittest.TestCase):
    def setUp(self) -> None:
        _WARNINGS.clear()
        GAME.XRSave.HAS_NATIVE_LZOKAY = False

    def test_unavailable_shim_keeps_game_features_and_lists_saves(self) -> None:
        self.assertFalse(sys.modules["lzokay"].AVAILABLE)
        game = GAME.StalkerAnomalyGame()

        class Organizer:
            def onAboutToRun(self, callback) -> None:  # noqa: ANN001
                self.callback = callback

        self.assertTrue(game.init(Organizer()))
        self.assertTrue(
            any(isinstance(feature, GAME.StalkerAnomalyModDataChecker)
                for feature in game.features)
        )
        self.assertTrue(
            any(isinstance(feature, GAME.StalkerAnomalyModDataContent)
                for feature in game.features)
        )
        self.assertTrue(any(isinstance(feature, _SaveInfo) for feature in game.features))
        self.assertFalse(
            any(isinstance(feature, GAME.StalkerAnomalySaveGameInfo)
                for feature in game.features)
        )
        self.assertTrue(any("save metadata is disabled" in warning for warning in _WARNINGS))

        with tempfile.TemporaryDirectory() as temporary:
            save = Path(temporary) / "manual-save.scop"
            save.write_bytes(b"not-a-parsed-save")
            listed = game.listSaves(_QDir(temporary))

        self.assertEqual(len(listed), 1)
        self.assertIs(type(listed[0]), _Save)
        self.assertEqual(Path(listed[0].getFilepath()).name, "manual-save.scop")

    def test_available_decoder_keeps_rich_save_enumeration(self) -> None:
        GAME.XRSave.HAS_NATIVE_LZOKAY = True
        game = GAME.StalkerAnomalyGame()

        class Organizer:
            def onAboutToRun(self, callback) -> None:  # noqa: ANN001
                self.callback = callback

        self.assertTrue(game.init(Organizer()))
        self.assertTrue(
            any(isinstance(feature, GAME.StalkerAnomalySaveGameInfo)
                for feature in game.features)
        )

        with tempfile.TemporaryDirectory() as temporary:
            save = Path(temporary) / "manual-save.scop"
            save.write_bytes(b"short")
            listed = game.listSaves(_QDir(temporary))

        self.assertEqual(len(listed), 1)
        self.assertIsInstance(listed[0], GAME.StalkerAnomalySaveGame)


if __name__ == "__main__":
    unittest.main()
