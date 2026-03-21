"""Configuration loading, saving, and default normalisation."""

import copy
import os
from pathlib import Path

import yaml

from frontier.cache import PROJECT_CACHE, path_signature
from frontier.utils import project_root

DEFAULT_CONFIG_PATH = project_root() / "configs" / "default.yaml"

_DEFAULTS = {
    "model": {
        "d_model": 768,
        "n_heads": 12,
        "n_layers": 14,
        "vocab_size": 32000,
        "max_seq_len": 2048,
        "dropout": 0.05,
        "gradient_checkpointing": True,
    },
    "training": {
        "batch_size": 8,
        "micro_batch_size": 2,
        "learning_rate": 2e-4,
        "weight_decay": 0.1,
        "warmup_steps": 1000,
        "max_epochs": 100,
        "max_steps": None,
        "batch_item_workers": 4,
        "prefetch_batches": 1,
        "checkpoint_every": 100,
        "best_checkpoint_count": 5,
        "vram_ceiling_gb": 5.5,
        "optimizer_cpu_offload": True,
        "mixed_precision": True,
        "gradient_accumulation_steps": 4,
        "max_grad_norm": 1.0,
        "label_smoothing": 0.1,
        "ema_decay": 0.999,
        "val_ratio": 0.05,
        "synthetic_samples_per_epoch": 50,
        "log_every": 10,
        "eval_every": 500,
    },
    "datasets": {
        "source_workers": 4,
        "trust_remote_code": False,
        "quality_threshold": 0.7,
        "tokenizer_vocab_size": 32000,
        "max_samples": 100000,
        "data_dir": "data/processed",
        "cache_dir": "data/cache",
    },
    "critic": {
        "enabled": True,
        "d_model": 256,
        "n_heads": 4,
        "n_layers": 4,
        "learning_rate": 1e-4,
        "reward_weight": 0.1,
    },
    "large_judge": {
        "enabled": True,
        "model_id": "Qwen/Qwen2.5-3B-Instruct",
        "fallback_model_ids": [
            "Qwen/Qwen2.5-1.5B-Instruct",
            "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
        ],
        "auto_download_required_models": True,
        "judge_interval_epochs": 5,
        "cache_dir": "data/cache/large_judge",
        "trust_remote_code": False,
        "protocols": ["rubric_scoring", "flaw_taxonomy", "chosen_rejected"],
    },
    "evaluation": {
        "cpu_threads": 4,
        "benchmarks": ["gsm8k", "reasoning_probes", "instruction_following"],
        "samples_per_benchmark": 100,
        "report_dir": "artifacts",
    },
    "curriculum": {
        "enabled": True,
        "current_stage": 1,
        "progression_threshold": 0.02,
        "patience_epochs": 3,
        "stages": {
            1: {"focus": "basic_completion", "mix": {"basic": 0.8, "reasoning": 0.2}},
            2: {"focus": "instruction_following", "mix": {"basic": 0.4, "reasoning": 0.4, "instruction": 0.2}},
            3: {"focus": "reasoning", "mix": {"basic": 0.2, "reasoning": 0.5, "instruction": 0.3}},
            4: {"focus": "complex_reasoning", "mix": {"reasoning": 0.4, "instruction": 0.3, "complex": 0.3}},
            5: {"focus": "mastery", "mix": {"reasoning": 0.3, "instruction": 0.3, "complex": 0.4}},
        },
    },
    "inference": {
        "host": "127.0.0.1",
        "port": 8766,
        "max_length": 512,
        "temperature": 0.7,
        "top_k": 50,
        "top_p": 0.9,
    },
    "dashboard": {
        "port": 8765,
        "poll_interval_ms": 3000,
    },
    "huggingface": {
        "token": "",
    },
}


def _deep_merge(base: dict, override: dict) -> dict:
    """Recursively merge *override* into a copy of *base*."""
    result = copy.deepcopy(base)
    for key, value in override.items():
        if key in result and isinstance(result[key], dict) and isinstance(value, dict):
            result[key] = _deep_merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def _normalize_stage_keys(config: dict) -> dict:
    """Ensure curriculum stage keys are integers, not strings from YAML/JSON."""
    stages = config.get("curriculum", {}).get("stages", {})
    if stages:
        normalized = {}
        for key, value in stages.items():
            try:
                normalized[int(key)] = value
            except (ValueError, TypeError):
                normalized[key] = value
        config.setdefault("curriculum", {})["stages"] = normalized
    return config


def _coerce_numeric_types(config: dict) -> dict:
    """Coerce known numeric config values that YAML/JSON may deliver as strings."""
    def _f(d, k, v):
        try:
            d[k] = float(d[k])
        except (KeyError, TypeError, ValueError):
            d[k] = v

    def _i(d, k, v):
        try:
            d[k] = int(d[k])
        except (KeyError, TypeError, ValueError):
            d[k] = v

    tc = config.get("training", {})
    for key, default in [
        ("learning_rate", 2e-4), ("weight_decay", 0.1), ("max_grad_norm", 1.0),
        ("label_smoothing", 0.1), ("ema_decay", 0.999), ("val_ratio", 0.05),
        ("vram_ceiling_gb", 5.5),
    ]:
        _f(tc, key, default)
    for key, default in [
        ("batch_size", 8), ("micro_batch_size", 2), ("gradient_accumulation_steps", 4),
        ("max_epochs", 100), ("warmup_steps", 1000), ("checkpoint_every", 100),
        ("eval_every", 500), ("log_every", 10), ("best_checkpoint_count", 5),
        ("batch_item_workers", 4), ("synthetic_samples_per_epoch", 0),
    ]:
        _i(tc, key, default)

    ds = config.get("datasets", {})
    _f(ds, "quality_threshold", 0.7)
    _i(ds, "source_workers", 4)
    _i(ds, "max_samples", 100000)

    cur = config.get("curriculum", {})
    _f(cur, "progression_threshold", 0.02)
    _i(cur, "patience_epochs", 3)
    _i(cur, "current_stage", 1)

    cc = config.get("critic", {})
    _f(cc, "reward_weight", 0.1)
    _f(cc, "learning_rate", 1e-4)

    return config


def normalize_defaults(config: dict) -> dict:
    """Fill any missing top-level/nested keys with built-in defaults."""
    config = _normalize_stage_keys(config)
    config = _deep_merge(_DEFAULTS, config)
    return _coerce_numeric_types(config)


def load_config(path=None) -> dict:
    """Load a YAML config file and normalise defaults."""
    path = Path(path) if path else DEFAULT_CONFIG_PATH
    cache_key = f"config:load:{path.resolve()}"

    def _load() -> dict:
        if not path.exists():
            return normalize_defaults({})
        with open(path, "r", encoding="utf-8") as f:
            raw = yaml.safe_load(f) or {}
        return normalize_defaults(raw)

    return PROJECT_CACHE.get_or_set(
        cache_key,
        _load,
        signature=path_signature(path),
    )


def save_config(config: dict, path=None) -> None:
    """Write *config* dict to a YAML file."""
    path = Path(path) if path else DEFAULT_CONFIG_PATH
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(config, f, default_flow_style=False, sort_keys=False)
    PROJECT_CACHE.invalidate(prefix="config:load:")


def get_nested(config: dict, dotpath: str, default=None):
    """Get a value from a nested dict using dot notation, e.g. 'training.batch_size'."""
    keys = dotpath.split(".")
    current = config
    for key in keys:
        if isinstance(current, dict) and key in current:
            current = current[key]
        else:
            return default
    return current


def resolve_huggingface_token(config: dict | None = None) -> str | None:
    """Return the configured Hugging Face token, falling back to environment."""
    config_token = ""
    if isinstance(config, dict):
        config_token = str(get_nested(config, "huggingface.token", "") or "").strip()
    if config_token:
        return config_token
    env_token = os.environ.get("HF_TOKEN", "").strip()
    return env_token or None
