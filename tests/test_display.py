from __future__ import annotations

import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

from PIL import Image

from inky_bird_frame.display import detect_display_size, show_on_inky
from inky_bird_frame.errors import MissingDependencyError


class _FakeDisplay:
    def __init__(self, width: int = 1600, height: int = 1200) -> None:
        self.width = width
        self.height = height
        self.image: object | None = None
        self.shown = False

    def set_image(self, image: object) -> None:
        self.image = image

    def show(self) -> None:
        self.shown = True


class DisplayTests(unittest.TestCase):
    def test_detect_display_size_prefers_xteink_backend(self) -> None:
        display = _FakeDisplay(width=1600, height=1200)
        xteink_module = SimpleNamespace(auto=lambda: display)

        with patch("inky_bird_frame.display.import_module", return_value=xteink_module):
            size = detect_display_size()

        self.assertEqual(size, (1600, 1200))

    def test_detect_display_size_falls_back_to_inky_backend(self) -> None:
        display = _FakeDisplay(width=1600, height=1200)
        inky_module = SimpleNamespace(auto=lambda: display)

        def importer(name: str) -> object:
            if name in {"xteink.x4", "xteink.auto"}:
                raise ModuleNotFoundError(name)
            if name == "inky.auto":
                return inky_module
            raise AssertionError(f"Unexpected import: {name}")

        with patch("inky_bird_frame.display.import_module", side_effect=importer):
            size = detect_display_size()

        self.assertEqual(size, (1600, 1200))

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
