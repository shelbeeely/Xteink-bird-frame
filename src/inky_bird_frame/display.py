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
    # Xteink X4 releases have exposed different constructor names:
    # auto (newer), x4/X4 (legacy aliases), and display (early preview builds).
    # Keep all of them for long-term backward compatibility with existing installations.
    _DisplayBackend("xteink.x4", ("auto", "x4", "X4", "display"), "Xteink X4"),
)


def _load_display() -> tuple[_InkyDisplay, str]:
    last_error: Exception | None = None
    backend_errors: list[str] = []
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
                except (OSError, RuntimeError) as exc:  # pragma: no cover - backend init failures
                    last_error = exc
                    backend_errors.append(f"{backend.module_name}.{factory_name}: {exc}")
                    continue
                return display, backend.label
    if last_error is not None:
        details = "; ".join(backend_errors)
        last_error.add_note(f"Backend initialization failures: {details}")
        raise last_error
    raise MissingDependencyError("Xteink X4 Python support is required for display output")


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
