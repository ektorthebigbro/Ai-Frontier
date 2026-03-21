"""Central large-judge model preset catalog, auto-download, and cache management."""

from contextlib import contextmanager
import copy
import logging
import os
import warnings
from pathlib import Path
from tqdm.auto import tqdm as _base_tqdm

from frontier.cache import PROJECT_CACHE, path_signature
from frontier.config import resolve_huggingface_token
from frontier.utils import project_root, append_jsonl, ensure_dir

log = logging.getLogger(__name__)
_EMITTED_HF_NOTES: set[str] = set()


# ---------------------------------------------------------------------------
# Model presets
# ---------------------------------------------------------------------------

MODEL_PRESETS = {
    "large_judge": [
        {
            "id": "Qwen/Qwen2.5-3B-Instruct",
            "label": "Qwen 2.5 3B Instruct",
            "description": "Primary judge — best quality",
            "vram_estimate_gb": 5.5,
            "recommended": True,
        },
        {
            "id": "Qwen/Qwen2.5-1.5B-Instruct",
            "label": "Qwen 2.5 1.5B Instruct",
            "description": "Fallback — lighter/faster",
            "vram_estimate_gb": 3.0,
            "recommended": False,
        },
        {
            "id": "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
            "label": "TinyLlama 1.1B Chat",
            "description": "Smallest fallback option",
            "vram_estimate_gb": 2.0,
            "recommended": False,
        },
    ],
}

ALLOWED_DOWNLOAD_PATTERNS = [
    "*.json",
    "*.model",
    "*.tiktoken",
    "*.txt",
    "*.safetensors",
    "*.bin",
    "tokenizer*",
    "special_tokens_map.json",
    "merges.txt",
    "vocab*",
]


def _format_bytes(size_bytes: float) -> str:
    size = float(max(0.0, size_bytes))
    units = ["B", "KB", "MB", "GB", "TB"]
    unit_idx = 0
    while size >= 1024.0 and unit_idx < len(units) - 1:
        size /= 1024.0
        unit_idx += 1
    precision = 0 if unit_idx == 0 else 1
    return f"{size:.{precision}f} {units[unit_idx]}"


def _emit_hf_note(key: str, message: str) -> None:
    if key in _EMITTED_HF_NOTES:
        return
    _EMITTED_HF_NOTES.add(key)
    log.info(message)


@contextmanager
def _hf_download_context():
    import huggingface_hub.constants as hf_constants

    previous_levels = {}
    logger_overrides = {
        "httpx": logging.WARNING,
        "httpcore": logging.WARNING,
        "huggingface_hub.file_download": logging.ERROR,
        "huggingface_hub.utils._http": logging.ERROR,
    }
    for logger_name, level in logger_overrides.items():
        logger_obj = logging.getLogger(logger_name)
        previous_levels[logger_name] = logger_obj.level
        logger_obj.setLevel(level)

    previous_symlink_env = os.environ.get("HF_HUB_DISABLE_SYMLINKS_WARNING")
    previous_symlink_const = hf_constants.HF_HUB_DISABLE_SYMLINKS_WARNING
    os.environ["HF_HUB_DISABLE_SYMLINKS_WARNING"] = "1"
    hf_constants.HF_HUB_DISABLE_SYMLINKS_WARNING = True

    try:
        with warnings.catch_warnings():
            warnings.filterwarnings(
                "ignore",
                message=r"`huggingface_hub` cache-system uses symlinks by default to.*",
                category=UserWarning,
            )
            yield
    finally:
        if previous_symlink_env is None:
            os.environ.pop("HF_HUB_DISABLE_SYMLINKS_WARNING", None)
        else:
            os.environ["HF_HUB_DISABLE_SYMLINKS_WARNING"] = previous_symlink_env
        hf_constants.HF_HUB_DISABLE_SYMLINKS_WARNING = previous_symlink_const
        for logger_name, level in previous_levels.items():
            logging.getLogger(logger_name).setLevel(level)


def get_model_catalog() -> dict:
    """Return the full preset catalog for dashboard display."""
    return copy.deepcopy(MODEL_PRESETS)


def default_model_id(group: str = "large_judge") -> str:
    """Return the default (recommended) model ID for a preset group."""
    presets = MODEL_PRESETS.get(group, [])
    for p in presets:
        if p.get("recommended"):
            return p["id"]
    return presets[0]["id"] if presets else ""


def required_model_ids(config: dict) -> list[str]:
    """Extract the list of model IDs needed from config."""
    judge_cfg = config.get("large_judge", {})
    if not judge_cfg.get("enabled", True):
        return []
    ids = []
    primary = judge_cfg.get("model_id", default_model_id())
    if primary:
        ids.append(primary)
    fallbacks = judge_cfg.get("fallback_model_ids", [])
    ids.extend(fallbacks)
    return ids


def apply_default_model_presets(config: dict) -> dict:
    """Return a new config with default model management settings filled in.

    Does not mutate the original config dict.
    """
    import copy
    config = copy.deepcopy(config)
    judge_cfg = config.setdefault("large_judge", {})
    judge_cfg.setdefault("enabled", True)
    judge_cfg.setdefault("model_id", default_model_id())
    judge_cfg.setdefault("fallback_model_ids", [
        "Qwen/Qwen2.5-1.5B-Instruct",
        "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
    ])
    judge_cfg.setdefault("auto_download_required_models", True)
    judge_cfg.setdefault("judge_interval_epochs", 5)
    judge_cfg.setdefault("cache_dir", "data/cache/large_judge")
    judge_cfg.setdefault("trust_remote_code", False)
    judge_cfg.setdefault("protocols", ["rubric_scoring", "flaw_taxonomy", "chosen_rejected"])
    return config


# ---------------------------------------------------------------------------
# Model cache inspection
# ---------------------------------------------------------------------------

def summarize_model_cache(config: dict) -> dict:
    """Scan the judge model cache directory and report what is available."""
    judge_cfg = config.get("large_judge", {})
    cache_dir = Path(project_root() / judge_cfg.get("cache_dir", "data/cache/large_judge"))
    cache_key = f"model_cache_summary:{cache_dir.resolve()}"

    def _scan() -> dict:
        summary: dict[str, dict] = {"large_judge": {}}

        if not cache_dir.exists():
            return summary

        for model_dir in cache_dir.iterdir():
            if not model_dir.is_dir():
                continue
            name = model_dir.name
            if name.startswith("models--"):
                model_id = name.replace("models--", "").replace("--", "/")
            else:
                model_id = name

            has_model = any(
                f.suffix in (".safetensors", ".bin")
                for f in model_dir.rglob("*") if f.is_file()
            )
            has_tokenizer = any(
                f.name.startswith("tokenizer") or f.name == "special_tokens_map.json"
                for f in model_dir.rglob("*") if f.is_file()
            )
            total_size_mb = sum(
                f.stat().st_size for f in model_dir.rglob("*") if f.is_file()
            ) // (1024 * 1024)

            summary["large_judge"][model_id] = {
                "cached": has_model and has_tokenizer,
                "has_model": has_model,
                "has_tokenizer": has_tokenizer,
                "size_mb": total_size_mb,
                "path": str(model_dir),
            }

        return summary

    return copy.deepcopy(PROJECT_CACHE.get_or_set(
        cache_key,
        _scan,
        ttl_seconds=15.0,
        signature=path_signature(cache_dir, include_children=True),
    ))


# ---------------------------------------------------------------------------
# Auto-download and model preflight
# ---------------------------------------------------------------------------

def _emit_progress(feed_path: str | None, job: str, stage: str,
                   message: str, progress: float):
    """Write a progress event to the dashboard feed."""
    print(f"AI_PROGRESS|{job}|{stage}|{progress:.4f}|{message}", flush=True)
    if feed_path is None:
        log.info("[%s] %s: %s (%.0f%%)", job, stage, message, progress * 100)
        return
    import time
    append_jsonl(feed_path, {
        "ts": time.time(),
        "job": job,
        "stage": stage,
        "message": message,
        "progress": round(progress, 4),
    })


class _SilentHubTqdm(_base_tqdm):
    """Silent tqdm wrapper compatible with HF's threaded download helpers."""

    def __init__(self, *args, **kwargs):
        kwargs.pop("name", None)
        kwargs["disable"] = False
        super().__init__(*args, **kwargs)

    def display(self, msg=None, pos=None):
        return None


def _plan_model_download(model_id: str, cache_dir: str, token: str | None = None) -> list:
    from huggingface_hub import snapshot_download

    with _hf_download_context():
        return snapshot_download(
            model_id,
            cache_dir=cache_dir,
            allow_patterns=ALLOWED_DOWNLOAD_PATTERNS,
            dry_run=True,
            token=token,
            tqdm_class=_SilentHubTqdm,
        )


def _download_plan_summary(download_plan: list) -> tuple[int, int, int]:
    pending_files = [info for info in download_plan if getattr(info, "will_download", True)]
    shard_count = sum(
        1 for info in pending_files
        if str(getattr(info, "filename", "")).endswith((".safetensors", ".bin"))
    )
    total_bytes = sum(int(getattr(info, "file_size", 0) or 0) for info in pending_files)
    return len(pending_files), shard_count, total_bytes


def _build_hf_progress_tqdm(feed_path: str | None,
                            job: str,
                            stage: str,
                            model_id: str,
                            total_bytes: int,
                            progress_start: float,
                            progress_end: float):
    from tqdm.auto import tqdm

    class _StructuredHubTqdm(tqdm):
        def __init__(self, *args, **kwargs):
            self._hf_name = kwargs.pop("name", "")
            self._last_bucket = -1
            self._max_seen_bytes = 0.0
            kwargs["disable"] = False
            super().__init__(*args, **kwargs)

        def display(self, msg=None, pos=None):
            return None

        def update(self, n=1):
            result = super().update(n)
            self._emit_if_needed()
            return result

        def refresh(self, *args, **kwargs):
            result = super().refresh(*args, **kwargs)
            self._emit_if_needed()
            return result

        def close(self):
            self._emit_if_needed(force=True)
            return super().close()

        def _emit_if_needed(self, force: bool = False) -> None:
            if getattr(self, "_hf_name", "") != "huggingface_hub.snapshot_download":
                return
            total = float(total_bytes or 0.0)
            if total <= 0.0:
                total = float(self.total or 0.0)
            if total <= 0.0:
                return
            self._max_seen_bytes = max(self._max_seen_bytes, float(self.n))
            current = min(self._max_seen_bytes, total)
            if current <= 0.0 and not force:
                return
            progress = max(0.0, min(1.0, current / total))
            bucket = 100 if progress >= 0.999 else int(progress * 100.0 // 5)
            if bucket == self._last_bucket:
                return
            self._last_bucket = bucket
            mapped = progress_start + (progress_end - progress_start) * progress
            message = f"Downloading {model_id}: {_format_bytes(current)} / {_format_bytes(total)}"
            _emit_progress(feed_path, job, stage, message, mapped)

    return _StructuredHubTqdm


def ensure_required_models(config: dict, dashboard_feed: str | None = None,
                           job: str = "prepare",
                           progress_start: float = 0.04,
                           progress_end: float = 0.14) -> bool:
    """Check and auto-download required judge models.

    Returns True if all required models are available.
    Emits progress stages: model_preflight -> model_check -> model_download -> model_ready
    """
    judge_cfg = config.get("large_judge", {})
    if not judge_cfg.get("enabled", True):
        return True

    auto_download = judge_cfg.get("auto_download_required_models", True)
    cache_dir = str(project_root() / judge_cfg.get("cache_dir", "data/cache/large_judge"))
    ensure_dir(cache_dir)
    hf_token = resolve_huggingface_token(config)

    model_ids = required_model_ids(config)
    if not model_ids:
        return True

    progress_range = progress_end - progress_start
    _emit_progress(dashboard_feed, job, "model_preflight",
                   f"Checking {len(model_ids)} required model(s)", progress_start)

    all_ok = True
    for i, model_id in enumerate(model_ids):
        step_progress = progress_start + (i / len(model_ids)) * progress_range
        _emit_progress(dashboard_feed, job, "model_check",
                       f"Checking: {model_id}", step_progress)

        # Try local cache first
        available = _check_local(model_id, cache_dir)
        if available:
            _emit_progress(dashboard_feed, job, "model_check",
                           f"Cached: {model_id}", step_progress + progress_range * 0.3 / len(model_ids))
            continue

        if not auto_download:
            log.warning("Model %s not cached and auto-download disabled", model_id)
            _emit_progress(dashboard_feed, job, "model_check",
                           f"Missing (no auto-download): {model_id}", step_progress)
            all_ok = False
            continue

        # Download
        _emit_progress(dashboard_feed, job, "model_download",
                       f"Downloading: {model_id}", step_progress)
        try:
            _download_model(
                model_id,
                cache_dir,
                token=hf_token,
                dashboard_feed=dashboard_feed,
                job=job,
                progress_start=step_progress,
                progress_end=step_progress + progress_range * 0.8 / len(model_ids),
            )
            _emit_progress(dashboard_feed, job, "model_download",
                           f"Downloaded: {model_id}",
                           step_progress + progress_range * 0.8 / len(model_ids))
        except Exception as exc:
            log.error("Failed to download %s: %s", model_id, exc)
            _emit_progress(dashboard_feed, job, "model_download",
                           f"Failed: {model_id} — {exc}", step_progress)
            all_ok = False

    _emit_progress(dashboard_feed, job, "model_ready",
                   "Model preflight complete" if all_ok else "Some models unavailable",
                   progress_end)
    return all_ok


def _check_local(model_id: str, cache_dir: str) -> bool:
    """Check if a model is available in local cache."""
    cache_key = f"model_local:{cache_dir}:{model_id}"

    def _probe() -> bool:
        try:
            from huggingface_hub import snapshot_download
            snapshot_download(model_id, cache_dir=cache_dir, local_files_only=True)
            return True
        except Exception:
            return False

    return bool(PROJECT_CACHE.get_or_set(cache_key, _probe, ttl_seconds=20.0))


def _download_model(model_id: str,
                    cache_dir: str,
                    token: str | None = None,
                    dashboard_feed: str | None = None,
                    job: str = "prepare",
                    progress_start: float = 0.0,
                    progress_end: float = 1.0) -> None:
    """Download a model from Hugging Face Hub with structured byte progress."""
    from huggingface_hub import snapshot_download
    import huggingface_hub.file_download as hf_file_download
    from huggingface_hub.utils import get_token

    with _hf_download_context():
        if token is None and get_token() is None:
            _emit_hf_note("hf_token_missing", "HF token not configured; using anonymous download limits.")
        symlinks_supported = hf_file_download.are_symlinks_supported(cache_dir)
        original_are_symlinks_supported = hf_file_download.are_symlinks_supported
        if os.name == "nt" and not symlinks_supported:
            _emit_hf_note(
                f"hf_symlink:{cache_dir}",
                "HF cache is using copies instead of symlinks on this Windows setup; downloads may use more disk space.",
            )
            hf_file_download.are_symlinks_supported = lambda *_args, **_kwargs: False

        try:
            plan = _plan_model_download(model_id, cache_dir, token=token)
            pending_files, shard_count, total_bytes = _download_plan_summary(plan)
            if pending_files <= 0:
                _emit_progress(
                    dashboard_feed,
                    job,
                    "model_download",
                    f"Using cached {model_id}",
                    progress_end,
                )
                PROJECT_CACHE.invalidate(prefix=f"model_local:{cache_dir}:")
                PROJECT_CACHE.invalidate(prefix="model_cache_summary:")
                return

            support_files = max(0, pending_files - shard_count)
            plan_message = (
                f"Downloading {model_id}: {shard_count} shard(s), {support_files} support file(s), "
                f"{_format_bytes(total_bytes)} total"
            )
            _emit_progress(dashboard_feed, job, "model_download", plan_message, progress_start)

            snapshot_download(
                model_id,
                cache_dir=cache_dir,
                allow_patterns=ALLOWED_DOWNLOAD_PATTERNS,
                token=token,
                tqdm_class=_build_hf_progress_tqdm(
                    feed_path=dashboard_feed,
                    job=job,
                    stage="model_download",
                    model_id=model_id,
                    total_bytes=total_bytes,
                    progress_start=progress_start,
                    progress_end=progress_end,
                ),
            )
        finally:
            hf_file_download.are_symlinks_supported = original_are_symlinks_supported
    PROJECT_CACHE.invalidate(prefix=f"model_local:{cache_dir}:")
    PROJECT_CACHE.invalidate(prefix="model_cache_summary:")
    log.info("Downloaded model: %s -> %s", model_id, cache_dir)
