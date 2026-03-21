"""General utility helpers for the AI Frontier project."""

import json
import logging
import os
import re
import time
from pathlib import Path

from frontier.cache import PROJECT_CACHE, path_signature

log = logging.getLogger(__name__)


def project_root() -> Path:
    """Return the absolute path to the project root directory."""
    return Path(__file__).resolve().parent.parent


def ensure_dir(path) -> Path:
    """Create directory (and parents) if it does not exist, return Path."""
    p = Path(path)
    p.mkdir(parents=True, exist_ok=True)
    return p


_CHECKPOINT_FILE_SUFFIXES = {".pt", ".pth", ".bin", ".safetensors"}


def checkpoint_artifact_path(path) -> Path | None:
    """Return the concrete checkpoint file for a checkpoint dir/file path."""
    if path is None:
        return None

    candidate = Path(path)
    if not candidate.exists():
        return None

    if candidate.is_dir():
        checkpoint_file = candidate / "checkpoint.pt"
        return checkpoint_file if checkpoint_file.is_file() else None

    if candidate.is_file() and (
        candidate.name == "checkpoint.pt" or candidate.suffix.lower() in _CHECKPOINT_FILE_SUFFIXES
    ):
        return candidate

    return None


def latest_checkpoint(checkpoint_dir) -> Path | None:
    """Return the most recent checkpoint directory/file, or None."""
    d = Path(checkpoint_dir)
    cache_key = f"latest_checkpoint:{d.resolve()}"

    def _load() -> Path | None:
        if not d.exists():
            return None
        candidates: list[tuple[float, Path]] = []
        for child in d.iterdir():
            if checkpoint_artifact_path(child) is None:
                continue
            try:
                candidates.append((child.stat().st_mtime, child))
            except OSError:
                continue
        candidates.sort(key=lambda item: item[0], reverse=True)
        for _, candidate in candidates:
            return candidate
        return None

    return PROJECT_CACHE.get_or_set(
        cache_key,
        _load,
        ttl_seconds=1.0,
        signature=path_signature(d, include_children=True),
    )


def format_eta(seconds: float | None) -> str:
    """Human-readable ETA string from seconds remaining."""
    if seconds is None or seconds < 0:
        return "unknown"
    seconds = int(seconds)
    if seconds < 60:
        return f"{seconds}s"
    minutes, secs = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes}m {secs}s"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h {minutes}m"


def safe_json_loads(text: str, default=None):
    """Parse JSON string, return *default* on failure."""
    try:
        return json.loads(text)
    except (json.JSONDecodeError, TypeError):
        return default


def recent_jsonl(path, n: int = 50) -> list[dict]:
    """Read the last *n* lines from a JSONL file, returning parsed dicts."""
    p = Path(path)
    cache_key = f"recent_jsonl:{p.resolve()}:{n}"

    def _load() -> list[dict]:
        if not p.exists():
            return []
        lines: list[str] = []
        try:
            with open(p, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except OSError:
            return []
        tail = lines[-n:] if len(lines) > n else lines
        results = []
        for line in tail:
            obj = safe_json_loads(line.strip())
            if obj is not None:
                results.append(obj)
        return results

    return PROJECT_CACHE.get_or_set(
        cache_key,
        _load,
        ttl_seconds=0.5,
        signature=path_signature(p),
    )


_JSONL_MAX_LINES = 5000


def _acquire_jsonl_lock(lock_path: Path, timeout_seconds: float = 1.5) -> bool:
    """Acquire a simple cross-process lock file for JSONL appends."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        try:
            fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            try:
                os.write(fd, str(os.getpid()).encode("utf-8"))
            finally:
                os.close(fd)
            return True
        except FileExistsError:
            try:
                age = time.time() - lock_path.stat().st_mtime
                if age > 30:
                    lock_path.unlink(missing_ok=True)
                    continue
            except OSError:
                pass
            time.sleep(0.01)
        except OSError:
            time.sleep(0.01)
    return False


def append_jsonl(path, obj: dict) -> None:
    """Append a single JSON object as a line to a JSONL file.

    Automatically truncates to the most recent ``_JSONL_MAX_LINES`` lines
    when the file exceeds twice that limit.
    """
    p = Path(path)
    ensure_dir(p.parent)
    lock_path = p.with_name(p.name + ".lock")
    if not _acquire_jsonl_lock(lock_path):
        log.warning("Timed out acquiring JSONL lock for %s", p)
        return
    try:
        with open(p, "a", encoding="utf-8") as f:
            f.write(json.dumps(obj, default=str) + "\n")
        # Periodic rotation check
        try:
            size = p.stat().st_size
            # Only check line count when file is large enough to possibly exceed the limit
            if size > _JSONL_MAX_LINES * 100:
                _rotate_jsonl(p, _JSONL_MAX_LINES)
        except OSError:
            pass
    finally:
        try:
            lock_path.unlink(missing_ok=True)
        except OSError:
            pass
    PROJECT_CACHE.invalidate(prefix=f"recent_jsonl:{p.resolve()}:")


def _rotate_jsonl(path: Path, keep_lines: int) -> None:
    """Truncate a JSONL file to the last *keep_lines* lines if it exceeds 2x that."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        if len(lines) <= keep_lines * 2:
            return
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(lines[-keep_lines:])
    except OSError:
        pass


def sanitize_filename(name: str) -> str:
    """Remove or replace characters that are unsafe in filenames."""
    return re.sub(r'[<>:"/\\|?*]', "_", name).strip()


class SentencePieceTokenizer:
    """Small adapter exposing a consistent encode/decode interface."""

    def __init__(self, processor):
        self.processor = processor
        self.eos_token_id = int(processor.eos_id())
        pad_token_id = int(processor.pad_id())
        if pad_token_id < 0:
            pad_token_id = self.eos_token_id if self.eos_token_id >= 0 else 0
        self.pad_token_id = pad_token_id

    def encode(self, text: str) -> list[int]:
        return [int(token_id) for token_id in self.processor.Encode(text)]

    def decode(self, token_ids: list[int]) -> str:
        return self.processor.Decode(token_ids)


def load_sentencepiece_tokenizer(path) -> SentencePieceTokenizer | None:
    """Load a SentencePiece tokenizer from *path*."""
    tokenizer_path = Path(path)
    cache_key = f"sentencepiece:{tokenizer_path.resolve()}"

    def _load() -> SentencePieceTokenizer | None:
        if not tokenizer_path.exists():
            return None
        try:
            import sentencepiece as spm
            processor = spm.SentencePieceProcessor()
            processor.Load(str(tokenizer_path))
            return SentencePieceTokenizer(processor)
        except Exception as exc:
            log.warning("Failed to load tokenizer %s: %s", tokenizer_path, exc)
            return None

    return PROJECT_CACHE.get_or_set(
        cache_key,
        _load,
        signature=path_signature(tokenizer_path),
    )


def _looks_like_state_dict(payload) -> bool:
    """Return True when *payload* looks like a raw model state_dict."""
    try:
        import torch
    except Exception:
        return False

    if not isinstance(payload, dict):
        return False
    if "model_state" in payload:
        return False
    if not payload:
        return True
    return all(isinstance(value, torch.Tensor) for value in payload.values())


def _normalize_checkpoint_payload(payload):
    """Wrap raw state_dict payloads into the checkpoint shape expected by callers."""
    if _looks_like_state_dict(payload):
        return {"model_state": payload}
    return payload


def safe_torch_load(path, map_location=None):
    """Load a torch checkpoint safely (weights_only=True)."""
    import torch

    checkpoint_path = Path(path)
    if checkpoint_path.suffix.lower() == ".safetensors":
        from safetensors.torch import load_file as load_safetensors_file

        return _normalize_checkpoint_payload(load_safetensors_file(str(checkpoint_path), device=str(map_location or "cpu")))

    try:
        return _normalize_checkpoint_payload(torch.load(path, map_location=map_location, weights_only=True))
    except TypeError:
        return _normalize_checkpoint_payload(torch.load(path, map_location=map_location))
    except RuntimeError as exc:
        if "weights only" not in str(exc).lower():
            raise
        log.warning("Falling back to full torch.load for %s due to checkpoint compatibility: %s", path, exc)
        return _normalize_checkpoint_payload(torch.load(path, map_location=map_location))
