"""Display adapter for supported e-paper hardware."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from importlib import import_module
from pathlib import Path
from typing import Protocol, cast

from .errors import MissingDependencyError


class _InkyDisplay(Protocol):
    width: int
    height: int

    def set_image(self, image: object) -> None: ...

    def show(self) -> None: ...


@dataclass(frozen=True)
class _DisplayBackend:
    module_name: str
    factory_names: tuple[str, ...]
    label: str


_DISPLAY_BACKENDS: tuple[_DisplayBackend, ...] = (
    # The Xteink X4 software stack has used multiple factory names across releases.
    _DisplayBackend("xteink.x4", ("auto", "x4", "X4", "display"), "Xteink X4"),
    _DisplayBackend("xteink.auto", ("auto",), "Xteink"),
    _DisplayBackend("inky.auto", ("auto",), "Pimoroni Inky"),
)


def _load_display() -> tuple[_InkyDisplay, str]:
    last_error: Exception | None = None
    for backend in _DISPLAY_BACKENDS:
        try:
            module = import_module(backend.module_name)
        except ModuleNotFoundError:
            continue
        for factory_name in backend.factory_names:
            factory = getattr(module, factory_name, None)
            if callable(factory):
                try:
                    display = cast(Callable[[], _InkyDisplay], factory)()
                except Exception as exc:  # pragma: no cover - hardware-specific failure path
                    last_error = exc
                    continue
                return display, backend.label
    if last_error is not None:
        raise last_error
    raise MissingDependencyError(
        "Xteink X4 or Pimoroni Inky Python support is required for display output"
    )


def detect_display_size() -> tuple[int, int]:
    display, _ = _load_display()
    return display.width, display.height


def show_on_inky(image_path: Path) -> tuple[int, int]:
    try:
        from PIL import Image
    except ModuleNotFoundError as exc:
        raise MissingDependencyError("Pillow is required to load display images") from exc

    image = Image.open(image_path).convert("RGB")
    display, _ = _load_display()
    expected_size = (display.width, display.height)
    if image.size != expected_size:
        raise ValueError(f"image size {image.size} does not match display size {expected_size}")
    display.set_image(image)
    display.show()
    return expected_size
