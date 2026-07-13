from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from PIL import Image

from inky_bird_frame import display as display_module
from inky_bird_frame.display import detect_display_size, show_on_inky
from inky_bird_frame.errors import MissingDependencyError


class _FakeDisplay:
    def __init__(self, width: int = 1600, height: int = 1200) -> None:
        self.width = width
        self.height = height
        self.image: Image.Image | None = None
        self.shown = False

    def set_image(self, image: Image.Image) -> None:
        self.image = image

    def show(self) -> None:
        self.shown = True


class DisplayTests(unittest.TestCase):
    @staticmethod
    def _xteink_module_names() -> set[str]:
        return {
            backend.module_name
            for backend in display_module._DISPLAY_BACKENDS
            if backend.label.startswith("Xteink")
        }

    @staticmethod
    def _xteink_x4_module_name() -> str:
        return next(
            backend.module_name
            for backend in display_module._DISPLAY_BACKENDS
            if backend.label == "Xteink X4"
        )

    @staticmethod
    def _xteink_auto_module_name() -> str:
        return next(
            backend.module_name
            for backend in display_module._DISPLAY_BACKENDS
            if backend.module_name == "xteink.auto"
        )

    def test_detect_display_size_prefers_xteink_backend(self) -> None:
        display = _FakeDisplay(width=1600, height=1200)
        xteink_module = SimpleNamespace(auto=lambda: display)

        with patch("inky_bird_frame.display.import_module", return_value=xteink_module):
            size = detect_display_size()

        self.assertEqual(size, (1600, 1200))

    def test_detect_display_size_falls_back_to_inky_backend(self) -> None:
        display = _FakeDisplay(width=1600, height=1200)
        inky_module = SimpleNamespace(auto=lambda: display)
        xteink_modules = self._xteink_module_names()

        def importer(name: str) -> object:
            if name in xteink_modules:
                raise ModuleNotFoundError(name)
            if name == "inky.auto":
                return inky_module
            raise AssertionError(f"Unexpected import: {name}")

        with patch("inky_bird_frame.display.import_module", side_effect=importer):
            size = detect_display_size()

        self.assertEqual(size, (1600, 1200))

    def test_detect_display_size_falls_back_when_xteink_init_fails(self) -> None:
        display = _FakeDisplay(width=1600, height=1200)
        xteink_module = SimpleNamespace(auto=self._simulate_xteink_init_error)
        inky_module = SimpleNamespace(auto=lambda: display)
        xteink_x4_module = self._xteink_x4_module_name()
        xteink_auto_module = self._xteink_auto_module_name()

        def importer(name: str) -> object:
            if name == xteink_x4_module:
                return xteink_module
            if name == xteink_auto_module:
                raise ModuleNotFoundError(name)
            if name == "inky.auto":
                return inky_module
            raise AssertionError(f"Unexpected import: {name}")

        with patch("inky_bird_frame.display.import_module", side_effect=importer):
            size = detect_display_size()

        self.assertEqual(size, (1600, 1200))

    @staticmethod
    def _simulate_xteink_init_error() -> _FakeDisplay:
        raise OSError("xteink failed")

    def test_show_on_inky_raises_when_no_backend_is_available(self) -> None:
        with (
            TemporaryDirectory() as temporary,
            patch("inky_bird_frame.display.import_module", side_effect=ModuleNotFoundError),
        ):
            image = Path(temporary) / "display.png"
            Image.new("RGB", (1600, 1200), "white").save(image)
            with self.assertRaises(MissingDependencyError):
                show_on_inky(image)


if __name__ == "__main__":
    unittest.main()
