"""Shared in-memory caching utilities for the AI Frontier project."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


_NO_SIGNATURE = object()


@dataclass
class CacheEntry:
    value: Any
    expires_at: float | None
    signature: Any = _NO_SIGNATURE


class ProjectCache:
    """Thread-safe cache with TTL and optional file/path signature support."""

    def __init__(self):
        self._entries: dict[str, CacheEntry] = {}
        self._lock = threading.RLock()

    def get_or_set(
        self,
        key: str,
        producer: Callable[[], Any],
        *,
        ttl_seconds: float | None = None,
        signature: Any = _NO_SIGNATURE,
    ) -> Any:
        now = time.monotonic()
        with self._lock:
            entry = self._entries.get(key)
            if entry is not None:
                not_expired = entry.expires_at is None or entry.expires_at > now
                signature_ok = signature is _NO_SIGNATURE or entry.signature == signature
                if not_expired and signature_ok:
                    return entry.value

        value = producer()
        expires_at = None if ttl_seconds is None else now + max(0.0, float(ttl_seconds))
        with self._lock:
            self._entries[key] = CacheEntry(value=value, expires_at=expires_at, signature=signature)
        return value

    def invalidate(self, *, key: str | None = None, prefix: str | None = None) -> int:
        with self._lock:
            if key is not None:
                return 1 if self._entries.pop(key, None) is not None else 0
            if prefix is not None:
                doomed = [name for name in self._entries if name.startswith(prefix)]
                for name in doomed:
                    self._entries.pop(name, None)
                return len(doomed)
            return 0

    def clear(self) -> None:
        with self._lock:
            self._entries.clear()

    def stats(self) -> dict[str, int]:
        with self._lock:
            return {"entries": len(self._entries)}


def path_signature(path, *, include_children: bool = False) -> tuple:
    """Return a lightweight path signature suitable for cache invalidation."""
    target = Path(path)
    if not target.exists():
        return ("missing", str(target))

    try:
        stat = target.stat()
    except OSError:
        return ("missing", str(target))

    if not target.is_dir():
        return ("file", str(target), stat.st_mtime_ns, stat.st_size)

    child_count = 0
    newest_child_mtime = 0
    if include_children:
        try:
            for child in target.iterdir():
                child_count += 1
                try:
                    newest_child_mtime = max(newest_child_mtime, child.stat().st_mtime_ns)
                except OSError:
                    continue
        except OSError:
            pass

    return ("dir", str(target), stat.st_mtime_ns, child_count, newest_child_mtime)


PROJECT_CACHE = ProjectCache()

