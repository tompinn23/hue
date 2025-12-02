from __future__ import annotations

from collections.abc import Generator
from typing import TYPE_CHECKING, Any

from ._core import (
    BackpackContents,
    FuelTanks,
    Module,
    ModuleEngineering,
    ModuleModifier,
    PowerplayInfo,
    Reader,
    State,
    Suit,
    SuitLoadout,
)

__all__ = [
    "BackpackContents",
    "FuelTanks",
    "Module",
    "ModuleEngineering",
    "ModuleModifier",
    "PowerplayInfo",
    "Reader",
    "State",
    "Suit",
    "SuitLoadout",
]

if TYPE_CHECKING:
    from typing import Protocol

    class AbstractEvent(Protocol):
        def is_set(self) -> bool: ...


def read(
    journal_dir: str, timeout: int = 0, stop_event: AbstractEvent | None = None
) -> Generator[tuple[State, dict[str, Any]]]:
    with Reader(journal_dir) as reader:
        for ev in reader.events(timeout, stop_event)
            if ev == 'stop':
                break
            if ev == 'timeout':
                break
            yield ev
