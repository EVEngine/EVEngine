"""Versioned machine-readable outcomes shared by EVEngine CI tools."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass
from enum import StrEnum
from pathlib import Path
from typing import Any


class FailureKind(StrEnum):
    """Stable top-level CI failure categories; values are public protocol data."""

    COMPILE = "compile"
    LINK = "link"
    CONTRACT = "contract"
    TEST_ASSERTION = "test_assertion"
    SANITIZER = "sanitizer"
    INFRASTRUCTURE = "infrastructure"
    PARITY = "parity"
    PUBLISH_CONFLICT = "publish_conflict"
    CANCELLED_SUPERSEDED = "cancelled_superseded"


@dataclass(frozen=True)
class CIResult:
    """One owning, serializable CI outcome with a stable schema."""

    ok: bool
    kind: str | None = None
    code: str | None = None
    subject: str | None = None
    message: str | None = None
    details: dict[str, Any] | None = None
    schema: str = "evengine.ci-result"
    version: int = 1

    @classmethod
    def success(cls, subject: str) -> CIResult:
        return cls(ok=True, subject=subject)

    @classmethod
    def failure(
        cls,
        kind: FailureKind,
        code: str,
        subject: str,
        message: str,
        details: dict[str, Any] | None = None,
    ) -> CIResult:
        return cls(False, kind.value, code, subject, message, details)

    def write(self, path: Path) -> None:
        """Atomically publish this result so partial JSON is never observable."""
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(json.dumps(asdict(self), indent=2) + "\n", encoding="utf-8")
        temporary.replace(path)
