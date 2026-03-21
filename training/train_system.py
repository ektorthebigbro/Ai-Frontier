"""Main training loop: generator + critic, curriculum, VRAM control, checkpointing."""

import argparse
from collections import deque
import json
import logging
import os
from pathlib import Path
import random
import sys
import time

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.tensorboard import SummaryWriter

from frontier.config import load_config
from frontier.data import (
    JsonlDataset, hardware_aware_workers, threaded_batch_load,
    prefetch_batch, get_prefetched, score_sample,
)
from frontier.hardware import autotune, VRAMCeiling, detect_gpu
from frontier.modeling import FrontierTransformer, CriticModel, ModelEMA
from frontier.optim import create_optimizer, create_scheduler, CPUOffloadOptimizer
from frontier.judging import CriticScorer, LargeJudge
from frontier.model_management import (
    apply_default_model_presets, ensure_required_models,
)
from frontier.utils import (
    project_root, ensure_dir, latest_checkpoint, checkpoint_artifact_path, append_jsonl, format_eta,
    load_sentencepiece_tokenizer, safe_torch_load,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("training")

FEED_PATH = str(project_root() / "logs" / "dashboard_metrics.jsonl")
CHECKPOINT_DIR = project_root() / "checkpoints"
LOG_DIR = project_root() / "logs"
RUNTIME_STATE_DIR = project_root() / ".tmp" / "runtime_state"
TRAINING_PAUSE_REQUEST_PATH = RUNTIME_STATE_DIR / "training.pause.request"
TRAINING_RECOVERY_STATE_PATH = RUNTIME_STATE_DIR / "training.recovery.json"


# ---------------------------------------------------------------------------
# Checkpoint save/load [Gap #8 — enhanced metadata]
# ---------------------------------------------------------------------------

def save_checkpoint(path: Path, epoch: int, step: int, model, critic_model,
                    optimizer, critic_optimizer, scheduler, rng_state,
                    best_loss: float, curriculum_stage: int,
                    dataset_position: int, config: dict, ema=None,
                    resume_next_epoch: bool = False):
    """Save a full training checkpoint with enhanced metadata and EMA weights."""
    ensure_dir(path)
    ckpt_data = {
        "epoch": epoch,
        "step": step,
        "model_state": model.state_dict(),
        "critic_state": critic_model.state_dict() if critic_model else None,
        "optimizer_state": optimizer.state_dict(),
        "critic_optimizer_state": critic_optimizer.state_dict() if critic_optimizer else None,
        "scheduler_state": scheduler.state_dict() if scheduler else None,
        "rng_python": random.getstate(),
        "rng_numpy": _serialize_numpy_state(np.random.get_state()),
        "rng_torch": torch.random.get_rng_state(),
        "rng_cuda": torch.cuda.get_rng_state_all() if torch.cuda.is_available() else None,
        "best_loss": best_loss,
        "curriculum_stage": curriculum_stage,
        "dataset_position": dataset_position,
        "resume_next_epoch": resume_next_epoch,
        "config": config,
        "timestamp": time.time(),
    }
    if ema is not None:
        ckpt_data["ema_state"] = ema.state_dict()
    torch.save(ckpt_data, path / "checkpoint.pt")
    log.info("Saved checkpoint: %s (epoch=%d, step=%d)", path, epoch, step)


def load_checkpoint(path: Path, model, critic_model, optimizer, critic_optimizer,
                    scheduler, device):
    """Load checkpoint and return metadata dict."""
    checkpoint_file = checkpoint_artifact_path(path)
    if checkpoint_file is None:
        raise FileNotFoundError(f"No checkpoint artifact found at {path}")
    ckpt = safe_torch_load(checkpoint_file, map_location=device)
    model.load_state_dict(ckpt["model_state"])
    if critic_model and ckpt.get("critic_state"):
        critic_model.load_state_dict(ckpt["critic_state"])
    optimizer.load_state_dict(ckpt["optimizer_state"])
    if critic_optimizer and ckpt.get("critic_optimizer_state"):
        critic_optimizer.load_state_dict(ckpt["critic_optimizer_state"])
    if scheduler and ckpt.get("scheduler_state"):
        scheduler.load_state_dict(ckpt["scheduler_state"])

    # Restore RNG state best-effort. Resume should not die because a legacy
    # checkpoint stored RNG blobs in a different container shape.
    try:
        random.setstate(_tupleize_python_state(ckpt["rng_python"]))
    except (TypeError, ValueError, KeyError) as exc:
        log.warning("Skipping Python RNG restore for %s: %s", path, exc)

    try:
        np.random.set_state(_deserialize_numpy_state(ckpt["rng_numpy"]))
    except (TypeError, ValueError, KeyError) as exc:
        log.warning("Skipping NumPy RNG restore for %s: %s", path, exc)

    try:
        torch_rng_state = _coerce_torch_rng_state(ckpt.get("rng_torch"))
        if torch_rng_state is not None:
            torch.random.set_rng_state(torch_rng_state)
    except (TypeError, ValueError) as exc:
        log.warning("Skipping torch RNG restore for %s: %s", path, exc)

    if torch.cuda.is_available() and ckpt.get("rng_cuda"):
        try:
            torch.cuda.set_rng_state_all(_coerce_cuda_rng_states(ckpt["rng_cuda"]))
        except (TypeError, ValueError) as exc:
            log.warning("Skipping CUDA RNG restore for %s: %s", path, exc)

    log.info("Loaded checkpoint: epoch=%d, step=%d", ckpt["epoch"], ckpt["step"])
    return ckpt


def _serialize_numpy_state(state) -> dict:
    """Convert NumPy RNG state into safe checkpoint primitives."""
    return {
        "bit_generator": state[0],
        "state": state[1].tolist(),
        "pos": int(state[2]),
        "has_gauss": int(state[3]),
        "cached_gaussian": float(state[4]),
    }


def _tupleize_python_state(state):
    """Recursively convert list-backed Python RNG state back to tuples."""
    if isinstance(state, list):
        return tuple(_tupleize_python_state(item) for item in state)
    return state


def _deserialize_numpy_state(state):
    """Restore a NumPy RNG state from checkpoint primitives."""
    if isinstance(state, dict) and "bit_generator" in state:
        return (
            state["bit_generator"],
            np.asarray(state["state"], dtype=np.uint32),
            int(state["pos"]),
            int(state["has_gauss"]),
            float(state["cached_gaussian"]),
        )
    return state


def _coerce_torch_rng_state(state):
    """Normalize a saved RNG blob into the CPU uint8 tensor PyTorch expects."""
    if state is None:
        return None
    if isinstance(state, torch.Tensor):
        tensor = state.detach()
        if tensor.device.type != "cpu":
            tensor = tensor.to("cpu")
        if tensor.dtype != torch.uint8:
            tensor = tensor.to(dtype=torch.uint8)
        return tensor.contiguous()
    if isinstance(state, np.ndarray):
        return torch.as_tensor(state, dtype=torch.uint8, device="cpu").contiguous()
    if isinstance(state, (bytes, bytearray)):
        return torch.tensor(list(state), dtype=torch.uint8)
    if isinstance(state, (list, tuple)):
        return torch.tensor(list(state), dtype=torch.uint8)
    raise TypeError(f"Unsupported RNG state type: {type(state).__name__}")


def _coerce_cuda_rng_states(state):
    """Normalize CUDA RNG state payloads for torch.cuda.set_rng_state_all()."""
    if isinstance(state, (list, tuple)):
        return [_coerce_torch_rng_state(item) for item in state if item is not None]
    coerced = _coerce_torch_rng_state(state)
    return [coerced] if coerced is not None else []


def rotate_checkpoints(checkpoint_dir: Path, keep_best: int = 3):
    """Delete old checkpoints, keep only the N most recent. [Gap #8]"""
    if not checkpoint_dir.exists():
        return
    ckpts = sorted(checkpoint_dir.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True)
    dirs = [c for c in ckpts if c.is_dir() and (c / "checkpoint.pt").exists()]
    for old in dirs[keep_best:]:
        import shutil
        shutil.rmtree(old, ignore_errors=True)
        log.info("Removed old checkpoint: %s", old)


def pause_requested() -> bool:
    """Return True when the native backend has requested a durable pause."""
    return TRAINING_PAUSE_REQUEST_PATH.exists()


def clear_pause_request() -> None:
    """Remove a pending durable pause request if present."""
    try:
        TRAINING_PAUSE_REQUEST_PATH.unlink(missing_ok=True)
    except OSError:
        pass


def write_training_recovery_state(payload: dict) -> None:
    """Persist training recovery metadata for the dashboard/backend."""
    try:
        ensure_dir(RUNTIME_STATE_DIR)
        temp_path = TRAINING_RECOVERY_STATE_PATH.with_suffix(".json.tmp")
        temp_path.write_text(json.dumps(payload, sort_keys=True), encoding="utf-8")
        temp_path.replace(TRAINING_RECOVERY_STATE_PATH)
    except (OSError, TypeError, ValueError) as exc:
        log.warning("Could not persist training recovery state: %s", exc)


def read_training_recovery_state() -> dict:
    """Load the last persisted training recovery snapshot if present."""
    try:
        if not TRAINING_RECOVERY_STATE_PATH.exists():
            return {}
        return json.loads(TRAINING_RECOVERY_STATE_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, TypeError, ValueError):
        return {}


def mark_training_failed(message: str) -> None:
    """Persist a terminal failure state for training and the dashboard feed."""
    state = read_training_recovery_state()
    progress = float(state.get("progress", 0.0) or 0.0)
    state.update({
        "ts": time.time(),
        "job": "training",
        "active": False,
        "paused": False,
        "message": message,
        "recovery_mode": state.get("recovery_mode", "checkpoint"),
    })
    write_training_recovery_state(state)
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "training",
        "stage": "failed",
        "message": message,
        "progress": round(progress, 4),
    })


def build_training_recovery_state(*, epoch: int, global_step: int, max_epochs: int,
                                  checkpoint_every: int, progress: float,
                                  last_checkpoint_name: str, last_checkpoint_epoch: int,
                                  last_checkpoint_step: int, last_checkpoint_ts: float,
                                  recovery_window_started_at: float | None,
                                  avg_step_seconds: float | None, paused: bool,
                                  active: bool, message: str) -> dict:
    """Build a compact recovery-state payload for the active training run."""
    now = time.time()
    next_checkpoint_step = None
    next_checkpoint_in_steps = None
    eta_to_next_checkpoint = None
    if checkpoint_every > 0:
        next_checkpoint_step = ((global_step // checkpoint_every) + 1) * checkpoint_every
        next_checkpoint_in_steps = max(0, next_checkpoint_step - global_step)
        if avg_step_seconds is not None:
            eta_to_next_checkpoint = max(0.0, next_checkpoint_in_steps * avg_step_seconds)

    seconds_since_checkpoint = None
    if last_checkpoint_ts > 0:
        seconds_since_checkpoint = max(0.0, now - last_checkpoint_ts)

    recovery_window_elapsed = None
    if recovery_window_started_at and recovery_window_started_at > 0:
        recovery_window_elapsed = max(0.0, now - recovery_window_started_at)

    recovery_window_expected = None
    if recovery_window_elapsed is not None and eta_to_next_checkpoint is not None:
        recovery_window_expected = max(recovery_window_elapsed, recovery_window_elapsed + eta_to_next_checkpoint)

    return {
        "ts": now,
        "job": "training",
        "active": active,
        "paused": paused,
        "message": message,
        "epoch": epoch,
        "global_step": global_step,
        "max_epochs": max_epochs,
        "progress": round(progress, 4),
        "checkpoint_every": checkpoint_every,
        "last_checkpoint_name": last_checkpoint_name,
        "last_checkpoint_epoch": last_checkpoint_epoch,
        "last_checkpoint_step": last_checkpoint_step,
        "last_checkpoint_ts": last_checkpoint_ts,
        "recovery_window_started_at": recovery_window_started_at,
        "recovery_window_elapsed_seconds": recovery_window_elapsed,
        "recovery_window_expected_seconds": recovery_window_expected,
        "seconds_since_last_checkpoint": seconds_since_checkpoint,
        "steps_since_last_checkpoint": max(0, global_step - max(last_checkpoint_step, 0)),
        "avg_step_seconds": avg_step_seconds,
        "next_checkpoint_step": next_checkpoint_step,
        "next_checkpoint_in_steps": next_checkpoint_in_steps,
        "eta_to_next_checkpoint_seconds": eta_to_next_checkpoint,
        "pause_creates_recovery_point": True,
        "recovery_mode": "checkpoint",
    }


def persist_training_recovery_state(*, epoch: int, global_step: int, max_epochs: int,
                                    checkpoint_every: int, progress: float,
                                    last_checkpoint_name: str, last_checkpoint_epoch: int,
                                    last_checkpoint_step: int, last_checkpoint_ts: float,
                                    recovery_window_started_at: float | None,
                                    avg_step_seconds: float | None, paused: bool,
                                    active: bool, message: str) -> None:
    """Write the current training recovery snapshot to disk."""
    write_training_recovery_state(build_training_recovery_state(
        epoch=epoch,
        global_step=global_step,
        max_epochs=max_epochs,
        checkpoint_every=checkpoint_every,
        progress=progress,
        last_checkpoint_name=last_checkpoint_name,
        last_checkpoint_epoch=last_checkpoint_epoch,
        last_checkpoint_step=last_checkpoint_step,
        last_checkpoint_ts=last_checkpoint_ts,
        recovery_window_started_at=recovery_window_started_at,
        avg_step_seconds=avg_step_seconds,
        paused=paused,
        active=active,
        message=message,
    ))


def emit_recovery_point(progress: float, epoch: int, global_step: int, checkpoint_name: str) -> None:
    """Append a structured recovery-point event into the dashboard feed."""
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "training",
        "stage": "recovery_point",
        "message": f"Recovery point saved: {checkpoint_name} (epoch {epoch} step {global_step})",
        "progress": round(progress, 4),
        "checkpoint_name": checkpoint_name,
        "epoch": epoch,
        "step": global_step,
    })


# ---------------------------------------------------------------------------
# Curriculum controller [Gap #5]
# ---------------------------------------------------------------------------

class CurriculumController:
    """Manages 5-stage curriculum with validation-loss-gated progression."""

    def __init__(self, config: dict):
        cur_cfg = config.get("curriculum", {})
        self.enabled = bool(cur_cfg.get("enabled", True))
        self.current_stage = int(cur_cfg.get("current_stage", 1) or 1)
        self.threshold = float(cur_cfg.get("progression_threshold", 0.02) or 0.02)
        self.patience = int(cur_cfg.get("patience_epochs", 3) or 3)
        self.stages = cur_cfg.get("stages", {})
        self._loss_history: list[float] = []
        self._stagnant_epochs = 0

    def get_mix(self) -> dict:
        """Return dataset mixing ratios for the current stage."""
        stage_key = int(self.current_stage)
        stage = self.stages.get(stage_key, self.stages.get(str(stage_key), {}))
        return stage.get("mix", {"basic": 1.0})

    def update(self, val_loss: float) -> bool:
        """Check if we should advance to next stage. Returns True if stage changed."""
        if not self.enabled or int(self.current_stage) >= 5:
            return False

        self._loss_history.append(float(val_loss))
        if len(self._loss_history) < 2:
            return False

        improvement = self._loss_history[-2] - self._loss_history[-1]
        if improvement < self.threshold:
            self._stagnant_epochs += 1
        else:
            self._stagnant_epochs = 0

        if self._stagnant_epochs >= self.patience:
            self.current_stage = min(5, self.current_stage + 1)
            self._stagnant_epochs = 0
            self._loss_history.clear()
            log.info("Curriculum: advancing to stage %d", self.current_stage)
            return True
        return False


# ---------------------------------------------------------------------------
# Synthetic generation [Gap #4]
# ---------------------------------------------------------------------------

def generate_synthetic_batch(model, tokenizer_path: str, config: dict,
                             device: torch.device, count: int = 10) -> list[dict]:
    """Generate synthetic reasoning samples using the current model."""
    try:
        import sentencepiece as spm
        sp = spm.SentencePieceProcessor()
        sp.Load(tokenizer_path)
    except Exception:
        log.warning("Cannot load tokenizer for synthetic generation")
        return []

    prompts = [
        # Arithmetic & math
        "Solve this step by step: What is 15 * 23?",
        "If a train travels at 60 km/h for 2.5 hours, how far does it go?",
        "Calculate the area of a circle with radius 7. Show your work.",
        "What is 347 + 289? Break it down step by step.",
        "A store has a 25% off sale. If a shirt costs $40, what is the sale price?",
        "What is the sum of the first 10 positive integers?",
        # Science
        "Explain why the sky appears blue.",
        "Describe the process of photosynthesis.",
        "What causes tides in the ocean?",
        "Explain how vaccines work in simple terms.",
        "What is the difference between DNA and RNA?",
        # General knowledge
        "What is the capital of France and why is it important?",
        "Explain the water cycle step by step.",
        "What are the three branches of the US government and what do they do?",
        "Describe the main differences between plants and animals.",
        # Reasoning & logic
        "If all cats are animals, and some animals are pets, can we conclude all cats are pets?",
        "A farmer has chickens and cows. He counts 20 heads and 56 legs. How many of each?",
        "What comes next in the pattern: 2, 6, 12, 20, 30, ?",
        "If it takes 5 machines 5 minutes to make 5 widgets, how long for 100 machines to make 100?",
        # Instruction following
        "List exactly three benefits of regular exercise. Number them 1, 2, 3.",
        "Summarize the concept of gravity in exactly two sentences.",
        "Write a short poem (4 lines) about the ocean.",
        "Explain recursion using a real-world analogy.",
        "Compare and contrast renewable and non-renewable energy sources.",
    ]

    samples = []
    model.eval()
    with torch.no_grad():
        for i in range(count):
            prompt = prompts[i % len(prompts)]
            ids = sp.Encode(prompt)
            input_ids = torch.tensor([ids], device=device)
            output_ids = model.generate(input_ids, max_new_tokens=256,
                                        temperature=0.8, top_k=50,
                                        eos_token_id=int(sp.eos_id()))
            generated = sp.Decode(output_ids[0].tolist()[len(ids):])

            sample = {
                "text": f"<think>\n{prompt}\n</think>\n"
                        f"<reason>\n{generated}\n</reason>\n"
                        f"<verify>\nChecking answer consistency.\n</verify>",
                "source": "synthetic_training",
            }
            sample["score"] = score_sample(sample)
            if sample["score"] > 0.3:  # rejection threshold
                samples.append(sample)

    model.train()
    return samples


# ---------------------------------------------------------------------------
# Training step
# ---------------------------------------------------------------------------

def train_step(model, batch_ids, batch_mask, scaler, config, device, pad_token_id: int):
    """Single forward-backward pass with mixed precision and label smoothing."""
    use_amp = bool(config.get("training", {}).get("mixed_precision", True))
    label_smoothing = float(config.get("training", {}).get("label_smoothing", 0.1) or 0.1)
    with torch.amp.autocast("cuda", enabled=use_amp and device.type == "cuda"):
        logits = model(batch_ids, batch_mask)
        # Shift for next-token prediction
        shift_logits = logits[:, :-1, :].contiguous()
        shift_labels = batch_ids[:, 1:].contiguous()
        loss = F.cross_entropy(shift_logits.view(-1, shift_logits.size(-1)),
                               shift_labels.view(-1), ignore_index=pad_token_id,
                               label_smoothing=label_smoothing)
    return loss


def checkpoint_and_pause(epoch: int, global_step: int, next_dataset_position: int,
                         model, critic_model, base_optimizer, critic_optimizer,
                         scheduler, best_loss: float, curriculum_stage: int,
                         config: dict, ema, progress: float, tb_writer,
                         keep_best: int, checkpoint_every: int,
                         max_epochs: int, avg_step_seconds: float | None) -> None:
    """Persist a resumable checkpoint and exit cleanly for backend pause."""
    ckpt_name = f"checkpoint_e{epoch}_s{global_step}"
    save_checkpoint(
        CHECKPOINT_DIR / ckpt_name,
        epoch,
        global_step,
        model,
        critic_model,
        base_optimizer,
        critic_optimizer,
        scheduler,
        None,
        best_loss,
        curriculum_stage,
        next_dataset_position,
        config,
        ema=ema,
        resume_next_epoch=False,
    )
    rotate_checkpoints(CHECKPOINT_DIR, keep_best=keep_best)
    emit_recovery_point(progress, epoch, global_step, ckpt_name)
    checkpoint_written_at = time.time()
    persist_training_recovery_state(
        epoch=epoch,
        global_step=global_step,
        max_epochs=max_epochs,
        checkpoint_every=checkpoint_every,
        progress=progress,
        last_checkpoint_name=ckpt_name,
        last_checkpoint_epoch=epoch,
        last_checkpoint_step=global_step,
        last_checkpoint_ts=checkpoint_written_at,
        recovery_window_started_at=checkpoint_written_at,
        avg_step_seconds=avg_step_seconds,
        paused=True,
        active=False,
        message=f"Paused with recovery point {ckpt_name}",
    )
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "training",
        "stage": "paused",
        "message": f"Training paused after epoch {epoch} step {global_step}",
        "progress": round(progress, 4),
    })
    tb_writer.flush()
    clear_pause_request()
    log.info("Pause request applied at epoch=%d step=%d", epoch, global_step)


def _compute_val_loss(model, ema, dataset, val_indices, mc, config, device,
                      max_batches: int = 20, pad_token_id: int = 0) -> float:
    """Compute validation loss using EMA weights on a held-out set."""
    if not val_indices:
        return float("inf")

    # Temporarily apply EMA weights
    orig_state = {n: p.data.clone() for n, p in model.named_parameters() if p.requires_grad}
    ema.apply(model)
    model.eval()

    total_loss = 0.0
    n_batches = 0
    max_len = int(mc.get("max_seq_len", 2048) or 2048)
    label_smoothing = float(config.get("training", {}).get("label_smoothing", 0.1) or 0.1)
    use_amp = bool(config.get("training", {}).get("mixed_precision", True))

    with torch.no_grad():
        for batch_start in range(0, min(len(val_indices), max_batches * 4), 4):
            batch_idx = val_indices[batch_start:batch_start + 4]
            items = [dataset[i] for i in batch_idx]
            all_ids = []
            for item in items:
                ids = item.get("input_ids", [])
                if isinstance(ids, list):
                    ids = ids[:max_len] + [pad_token_id] * (max_len - len(ids[:max_len]))
                    all_ids.append(ids)
            if not all_ids:
                continue

            batch_ids = torch.tensor(all_ids, device=device)
            batch_mask = (batch_ids != pad_token_id).long()

            with torch.amp.autocast("cuda", enabled=use_amp and device.type == "cuda"):
                logits = model(batch_ids, batch_mask)
                shift_logits = logits[:, :-1, :].contiguous()
                shift_labels = batch_ids[:, 1:].contiguous()
                loss = F.cross_entropy(shift_logits.view(-1, shift_logits.size(-1)),
                                       shift_labels.view(-1), ignore_index=pad_token_id,
                                       label_smoothing=label_smoothing)
            total_loss += loss.item()
            n_batches += 1
            if n_batches >= max_batches:
                break

    # Restore original weights
    with torch.no_grad():
        for name, param in model.named_parameters():
            if name in orig_state:
                param.data.copy_(orig_state[name])
    model.train()

    return total_loss / max(n_batches, 1)


# ---------------------------------------------------------------------------
# Config type coercion — defensive guard against string values from YAML/JSON
# ---------------------------------------------------------------------------

def _coerce_config_types(config: dict) -> None:
    """Coerce all known numeric config values in-place.

    The C++ SimpleYaml writer can occasionally serialise numbers as quoted
    strings, and yaml.safe_load returns them as str.  This guard ensures every
    field used in arithmetic or comparison is the correct Python type before
    any training code runs.
    """
    def _f(d: dict, key: str, default: float) -> None:
        try:
            d[key] = float(d[key])
        except (KeyError, TypeError, ValueError):
            d[key] = default

    def _i(d: dict, key: str, default: int) -> None:
        try:
            d[key] = int(d[key])
        except (KeyError, TypeError, ValueError):
            d[key] = default

    def _b(d: dict, key: str, default: bool) -> None:
        v = d.get(key, default)
        if isinstance(v, str):
            d[key] = v.lower() not in ("false", "0", "no", "")
        else:
            d[key] = bool(v)

    tc = config.setdefault("training", {})
    _f(tc, "learning_rate", 2e-4)
    _f(tc, "weight_decay", 0.1)
    _f(tc, "max_grad_norm", 1.0)
    _f(tc, "label_smoothing", 0.1)
    _f(tc, "ema_decay", 0.999)
    _f(tc, "val_ratio", 0.05)
    _f(tc, "vram_ceiling_gb", 5.5)
    _i(tc, "batch_size", 8)
    _i(tc, "micro_batch_size", 2)
    _i(tc, "gradient_accumulation_steps", 4)
    _i(tc, "max_epochs", 100)
    _i(tc, "warmup_steps", 1000)
    _i(tc, "checkpoint_every", 100)
    _i(tc, "eval_every", 500)
    _i(tc, "log_every", 10)
    _i(tc, "best_checkpoint_count", 5)
    _i(tc, "batch_item_workers", 4)
    _i(tc, "prefetch_batches", 1)
    _i(tc, "synthetic_samples_per_epoch", 0)
    _b(tc, "mixed_precision", True)
    _b(tc, "optimizer_cpu_offload", True)
    _b(tc, "gradient_checkpointing", True)

    mc = config.setdefault("model", {})
    _i(mc, "d_model", 768)
    _i(mc, "n_heads", 12)
    _i(mc, "n_layers", 14)
    _i(mc, "vocab_size", 32000)
    _i(mc, "max_seq_len", 2048)
    _f(mc, "dropout", 0.05)
    _b(mc, "gradient_checkpointing", True)

    cc = config.setdefault("critic", {})
    _f(cc, "learning_rate", 1e-4)
    _f(cc, "reward_weight", 0.1)
    _i(cc, "d_model", 256)
    _i(cc, "n_heads", 4)
    _i(cc, "n_layers", 4)
    _b(cc, "enabled", True)

    cur = config.setdefault("curriculum", {})
    _f(cur, "progression_threshold", 0.02)
    _i(cur, "patience_epochs", 3)
    _i(cur, "current_stage", 1)
    _b(cur, "enabled", True)

    ds = config.setdefault("datasets", {})
    _f(ds, "quality_threshold", 0.7)
    _i(ds, "source_workers", 4)
    _i(ds, "max_samples", 100000)
    _i(ds, "tokenizer_vocab_size", 32000)

    jc = config.setdefault("large_judge", {})
    _i(jc, "judge_interval_epochs", 5)
    _b(jc, "enabled", True)
    _b(jc, "auto_download_required_models", True)
    _b(jc, "trust_remote_code", False)

    inf = config.setdefault("inference", {})
    _f(inf, "temperature", 0.7)
    _f(inf, "top_p", 0.9)
    _i(inf, "top_k", 50)
    _i(inf, "max_length", 512)
    _i(inf, "port", 8766)


# ---------------------------------------------------------------------------
# Main training loop
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="AI Frontier: Training System")
    parser.add_argument("--config", default=str(project_root() / "configs" / "default.yaml"))
    parser.add_argument("--resume", action="store_true", help="Resume from latest checkpoint")
    args = parser.parse_args()

    config = apply_default_model_presets(load_config(args.config))
    config = autotune(config)
    _coerce_config_types(config)
    tc = config["training"]
    mc = config["model"]

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log.info("Device: %s", device)
    log.info("GPU: %s", detect_gpu())

    ensure_dir(LOG_DIR)
    ensure_dir(CHECKPOINT_DIR)
    ensure_dir(RUNTIME_STATE_DIR)
    clear_pause_request()

    append_jsonl(FEED_PATH, {
        "ts": time.time(), "job": "training", "stage": "starting",
        "message": "Training starting", "progress": 0.0,
    })

    # Model preflight
    ensure_required_models(config, FEED_PATH, job="training",
                           progress_start=0.05, progress_end=0.10)

    # Initialise models
    log.info("Initialising generator model")
    model = FrontierTransformer(config).to(device)

    critic_model = None
    critic_scorer = None
    critic_optimizer = None
    if config.get("critic", {}).get("enabled", True):
        log.info("Initialising critic model")
        critic_model = CriticModel(config).to(device)
        critic_scorer = CriticScorer(critic_model, device)
        critic_optimizer = torch.optim.AdamW(
            critic_model.parameters(),
            lr=float(config["critic"].get("learning_rate", 1e-4) or 1e-4))

    # EMA for better final model quality
    ema_decay = float(tc.get("ema_decay", 0.999) or 0.999)
    ema = ModelEMA(model, decay=ema_decay)
    log.info("EMA initialized (decay=%.4f)", ema_decay)

    # Optimiser
    base_optimizer = create_optimizer(model, config)
    if tc.get("optimizer_cpu_offload", True) and device.type == "cuda":
        optimizer = CPUOffloadOptimizer(base_optimizer, device)
        log.info("Using CPU offload optimizer")
    else:
        optimizer = base_optimizer

    # VRAM ceiling
    vram_ceiling = VRAMCeiling(ceiling_gb=float(tc.get("vram_ceiling_gb", 5.5) or 5.5))

    # Dataset
    data_dir = project_root() / config["datasets"].get("data_dir", "data/processed")
    dataset_path = data_dir / "train_scored.jsonl"
    tokenizer_path = data_dir / "tokenizer.model"
    tokenizer = load_sentencepiece_tokenizer(tokenizer_path)
    if tokenizer is None:
        log.error("No tokenizer found at %s - run prepare first", tokenizer_path)
        mark_training_failed(f"No tokenizer found at {tokenizer_path}")
        return 1
    pad_token_id = int(getattr(tokenizer, "pad_token_id", 0))
    full_dataset = JsonlDataset(dataset_path, tokenizer=tokenizer, max_seq_len=int(mc.get("max_seq_len", 2048) or 2048))
    if len(full_dataset) == 0:
        log.error("No training data found at %s — run prepare first", dataset_path)
        mark_training_failed(f"No training data found at {dataset_path}")
        return 1

    # Train/validation split (5% val)
    val_ratio = float(tc.get("val_ratio", 0.05) or 0.05)
    if len(full_dataset) > 1:
        n_val = min(len(full_dataset) - 1, max(1, int(len(full_dataset) * val_ratio)))
    else:
        n_val = 0
    n_train = len(full_dataset) - n_val
    _raw_seed = tc.get("data_order_seed", 1337)
    data_order_seed = int(_raw_seed) if _raw_seed is not None else 1337
    split_rng = random.Random(data_order_seed)
    all_indices = list(range(len(full_dataset)))
    split_rng.shuffle(all_indices)
    train_indices = all_indices[:n_train]
    val_indices = all_indices[n_train:]
    dataset = full_dataset  # keep reference for indexing
    log.info("Train/val split: %d train, %d val (%.1f%%)", n_train, n_val, val_ratio * 100)

    # Scheduler — attach to the same optimizer reference used for scaler.step()
    _max_epochs = int(tc.get("max_epochs", 100))
    total_steps = max(1, (len(dataset) // max(int(tc["batch_size"]), 1)) * _max_epochs)
    scheduler = create_scheduler(optimizer, config, total_steps)

    # Curriculum
    curriculum = CurriculumController(config)

    # TensorBoard
    tb_writer = SummaryWriter(log_dir=str(LOG_DIR / "tensorboard"))

    # Mixed precision scaler
    scaler = torch.amp.GradScaler("cuda", enabled=tc.get("mixed_precision", True) and device.type == "cuda")

    # Resume from checkpoint
    start_epoch = 0
    global_step = 0
    best_loss = float("inf")
    dataset_position = 0
    last_checkpoint_name = ""
    last_checkpoint_epoch = 0
    last_checkpoint_step = 0
    last_checkpoint_ts = 0.0
    recovery_window_started_at = time.time()

    if args.resume:
        ckpt_path = latest_checkpoint(CHECKPOINT_DIR)
        ckpt_file = checkpoint_artifact_path(ckpt_path)
        if ckpt_file is not None:
            ckpt = load_checkpoint(ckpt_path, model, critic_model,
                                   base_optimizer, critic_optimizer, scheduler, device)
            resume_next_epoch = bool(ckpt.get("resume_next_epoch", False))
            start_epoch = int(ckpt["epoch"]) + 1 if resume_next_epoch else int(ckpt["epoch"])
            global_step = int(ckpt["step"])
            _raw_best_loss = ckpt.get("best_loss")
            best_loss = float(_raw_best_loss) if _raw_best_loss is not None else float("inf")
            curriculum.current_stage = int(ckpt.get("curriculum_stage", 1) or 1)
            dataset_position = 0 if resume_next_epoch else max(0, int(ckpt.get("dataset_position", 0) or 0))
            if ckpt.get("ema_state"):
                ema.load_state_dict(ckpt["ema_state"])
                log.info("Restored EMA state from checkpoint")
            last_checkpoint_name = ckpt_path.name
            last_checkpoint_epoch = int(ckpt["epoch"])
            last_checkpoint_step = int(ckpt["step"])
            last_checkpoint_ts = float(ckpt.get("timestamp", ckpt_file.stat().st_mtime) or ckpt_file.stat().st_mtime)
            recovery_window_started_at = time.time()
            log.info("Resumed from epoch %d, step %d (resume_next_epoch=%s)",
                     start_epoch, global_step, resume_next_epoch)

    # Large judge (periodic)
    large_judge = None
    judge_cfg = config.get("large_judge", {})
    if judge_cfg.get("enabled", True):
        large_judge = LargeJudge(
            judge_cfg["model_id"],
            str(project_root() / judge_cfg.get("cache_dir", "data/cache/large_judge")),
            device="cpu",  # keep on CPU, load to GPU only during judge passes
            trust_remote_code=judge_cfg.get("trust_remote_code", False),
        )

    # Training configuration
    micro_batch = int(tc.get("micro_batch_size", 2) or 2)
    grad_accum = int(tc.get("gradient_accumulation_steps", 4) or 4)
    max_grad_norm = float(tc.get("max_grad_norm", 1.0) or 1.0)
    workers = hardware_aware_workers(config)
    log_every = int(tc.get("log_every", 10) or 10)
    ckpt_every = int(tc.get("checkpoint_every", 500) or 500)
    eval_every = int(tc.get("eval_every", 500) or 500)
    judge_interval = int(judge_cfg.get("judge_interval_epochs", 5) or 5)
    best_ckpt_count = int(tc.get("best_checkpoint_count", 3) or 3)
    reward_weight = float(config.get("critic", {}).get("reward_weight", 0.1) or 0.1)
    recent_step_durations = deque(maxlen=48)
    last_step_wall_time = None
    average_step_seconds = None

    log.info("Starting training: epochs=%d, batch=%d, micro=%d, grad_accum=%d",
             _max_epochs, tc["batch_size"], micro_batch, grad_accum)

    append_jsonl(FEED_PATH, {
        "ts": time.time(), "job": "training", "stage": "training",
        "message": f"Training epoch {start_epoch}", "progress": 0.10,
    })
    persist_training_recovery_state(
        epoch=start_epoch,
        global_step=global_step,
        max_epochs=_max_epochs,
        checkpoint_every=ckpt_every,
        progress=0.10,
        last_checkpoint_name=last_checkpoint_name,
        last_checkpoint_epoch=last_checkpoint_epoch,
        last_checkpoint_step=last_checkpoint_step,
        last_checkpoint_ts=last_checkpoint_ts,
        recovery_window_started_at=recovery_window_started_at,
        avg_step_seconds=average_step_seconds,
        paused=False,
        active=True,
        message=f"Training active from epoch {start_epoch}",
    )

    # ---------------------------------------------------------------------------
    # Epoch loop
    # ---------------------------------------------------------------------------
    for epoch in range(start_epoch, _max_epochs):
        model.train()
        epoch_loss = 0.0
        epoch_steps = 0
        progress = 0.10 + 0.85 * (epoch / max(_max_epochs, 1))

        # Log curriculum mix for this epoch
        mix = curriculum.get_mix()
        log.info("Epoch %d: curriculum stage=%d, mix=%s", epoch, curriculum.current_stage, mix)
        tb_writer.add_scalars("curriculum/mix", mix, epoch)

        indices = list(train_indices)
        random.Random(data_order_seed + epoch + 1).shuffle(indices)

        # Start from saved position if resuming
        if dataset_position > 0 and epoch == start_epoch:
            resume_offset = min(dataset_position, len(indices))
            if resume_offset != dataset_position:
                log.warning("Checkpoint dataset position %d exceeded epoch size %d; clamping resume offset",
                            dataset_position, len(indices))
            indices = indices[resume_offset:]
            dataset_position = 0

        batch_start = 0
        while batch_start < len(indices):
            batch_end = min(batch_start + int(tc["batch_size"]), len(indices))
            batch_indices = indices[batch_start:batch_end]

            # VRAM ceiling check [Gap #2] — reset from config so size can recover
            micro_batch = vram_ceiling.recommend_batch_size(int(tc.get("micro_batch_size", 2) or 2))

            # Prefetch next batch
            next_start = batch_end
            if next_start < len(indices):
                next_indices = indices[next_start:min(next_start + int(tc["batch_size"]), len(indices))]
                prefetch_batch(dataset, next_indices, workers)

            # Load current batch (use prefetched if available)
            prefetched = get_prefetched(batch_indices)
            if prefetched is not None:
                batch_items = prefetched
            else:
                batch_items = threaded_batch_load(dataset, batch_indices, workers)

            # Prepare tensors
            max_len = int(mc.get("max_seq_len", 2048) or 2048)
            all_ids = []
            for item in batch_items:
                ids = item.get("input_ids", [])
                if isinstance(ids, list):
                    ids = ids[:max_len]
                    # Pad to max_len
                    ids = ids + [pad_token_id] * (max_len - len(ids))
                    all_ids.append(ids)

            if not all_ids:
                batch_start = batch_end
                continue

            # Process micro-batches with gradient accumulation
            optimizer.zero_grad()
            batch_loss_total = 0.0
            actual_accum_steps = max(1, (len(all_ids) + micro_batch - 1) // micro_batch)

            for micro_start in range(0, len(all_ids), micro_batch):
                micro_end = min(micro_start + micro_batch, len(all_ids))
                micro_ids = torch.tensor(all_ids[micro_start:micro_end], device=device)
                micro_mask = (micro_ids != pad_token_id).long()

                loss = train_step(model, micro_ids, micro_mask, scaler, config, device, pad_token_id)

                # Critic reward shaping
                if critic_scorer is not None:
                    with torch.no_grad():
                        rewards = critic_scorer.score(micro_ids)
                        reward_bonus = rewards.mean() * reward_weight
                    loss = loss - reward_bonus

                batch_loss_total += loss.item()
                scaler.scale(loss / actual_accum_steps).backward()

            # Gradient clipping and optimizer step
            # Use the same optimizer reference for unscale_ and step to avoid
            # GradScaler tracking mismatch (it keys on id(optimizer)).
            scaler.unscale_(optimizer)
            torch.nn.utils.clip_grad_norm_(model.parameters(), max_grad_norm)
            scaler.step(optimizer)
            scaler.update()
            scheduler.step()
            ema.update(model)

            batch_loss = batch_loss_total / actual_accum_steps
            global_step += 1
            epoch_loss += batch_loss
            epoch_steps += 1
            progress = 0.10 + 0.85 * ((epoch + batch_end / max(len(indices), 1)) / _max_epochs)
            now = time.time()
            if last_step_wall_time is not None:
                recent_step_durations.append(max(0.0, now - last_step_wall_time))
                if recent_step_durations:
                    average_step_seconds = sum(recent_step_durations) / len(recent_step_durations)
            last_step_wall_time = now

            # Logging
            if global_step % log_every == 0:
                avg_loss = epoch_loss / max(epoch_steps, 1)
                vram = vram_ceiling.check_pressure()
                lr = scheduler.get_last_lr()[0]

                tb_writer.add_scalar("train/loss", batch_loss, global_step)
                tb_writer.add_scalar("train/avg_loss", avg_loss, global_step)
                tb_writer.add_scalar("train/lr", lr, global_step)
                tb_writer.add_scalar("train/vram_pressure", vram, global_step)
                tb_writer.add_scalar("train/micro_batch", micro_batch, global_step)
                tb_writer.add_scalar("train/accumulation_steps", actual_accum_steps, global_step)
                tb_writer.add_scalar("train/curriculum_stage", curriculum.current_stage, global_step)

                append_jsonl(FEED_PATH, {
                    "ts": time.time(), "job": "training", "stage": "training",
                    "message": f"Epoch {epoch} step {global_step} loss={batch_loss:.4f} lr={lr:.2e}",
                    "progress": round(progress, 4),
                })
                persist_training_recovery_state(
                    epoch=epoch,
                    global_step=global_step,
                    max_epochs=_max_epochs,
                    checkpoint_every=ckpt_every,
                    progress=progress,
                    last_checkpoint_name=last_checkpoint_name,
                    last_checkpoint_epoch=last_checkpoint_epoch,
                    last_checkpoint_step=last_checkpoint_step,
                    last_checkpoint_ts=last_checkpoint_ts,
                    recovery_window_started_at=recovery_window_started_at,
                    avg_step_seconds=average_step_seconds,
                    paused=False,
                    active=True,
                    message=f"Training active at epoch {epoch} step {global_step}",
                )

            # Checkpoint
            if global_step % ckpt_every == 0:
                ckpt_name = f"checkpoint_e{epoch}_s{global_step}"
                ckpt_path = CHECKPOINT_DIR / ckpt_name
                save_checkpoint(
                    ckpt_path, epoch, global_step, model, critic_model,
                    base_optimizer, critic_optimizer, scheduler,
                    None, best_loss, curriculum.current_stage,
                    batch_end, config, ema=ema, resume_next_epoch=False,
                )
                current_loss = epoch_loss / max(epoch_steps, 1)
                if current_loss < best_loss:
                    best_loss = current_loss
                rotate_checkpoints(CHECKPOINT_DIR, keep_best=best_ckpt_count)
                last_checkpoint_name = ckpt_name
                last_checkpoint_epoch = epoch
                last_checkpoint_step = global_step
                last_checkpoint_ts = time.time()
                recovery_window_started_at = last_checkpoint_ts
                emit_recovery_point(progress, epoch, global_step, ckpt_name)
                persist_training_recovery_state(
                    epoch=epoch,
                    global_step=global_step,
                    max_epochs=_max_epochs,
                    checkpoint_every=ckpt_every,
                    progress=progress,
                    last_checkpoint_name=last_checkpoint_name,
                    last_checkpoint_epoch=last_checkpoint_epoch,
                    last_checkpoint_step=last_checkpoint_step,
                    last_checkpoint_ts=last_checkpoint_ts,
                    recovery_window_started_at=recovery_window_started_at,
                    avg_step_seconds=average_step_seconds,
                    paused=False,
                    active=True,
                    message=f"Recovery point ready at step {global_step}",
                )

            if pause_requested():
                checkpoint_and_pause(
                    epoch,
                    global_step,
                    batch_end,
                    model,
                    critic_model,
                    base_optimizer,
                    critic_optimizer,
                    scheduler,
                    best_loss,
                    curriculum.current_stage,
                    config,
                    ema,
                    round(progress, 4),
                    tb_writer,
                    best_ckpt_count,
                    ckpt_every,
                    _max_epochs,
                    average_step_seconds,
                )
                tb_writer.close()
                return

            batch_start = batch_end

            # VRAM cleanup periodically
            if global_step % 50 == 0:
                vram_ceiling.try_clear_cache()

        # End of epoch
        avg_epoch_loss = epoch_loss / max(epoch_steps, 1)
        log.info("Epoch %d complete: avg_loss=%.4f, steps=%d", epoch, avg_epoch_loss, epoch_steps)

        # Validation loss (using EMA weights for fair evaluation)
        val_loss = _compute_val_loss(
            model, ema, dataset, val_indices, mc, config, device, pad_token_id=pad_token_id
        )
        log.info("Epoch %d validation loss: %.4f", epoch, val_loss)
        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "training",
            "stage": "validation",
            "message": f"Epoch {epoch} validation loss={val_loss:.4f}",
            "progress": round(progress, 4),
            "epoch": epoch,
        })

        tb_writer.add_scalar("epoch/train_loss", avg_epoch_loss, epoch)
        tb_writer.add_scalar("epoch/val_loss", val_loss, epoch)
        tb_writer.add_scalar("epoch/curriculum_stage", curriculum.current_stage, epoch)

        # Curriculum update — use validation loss for progression decisions
        stage_changed = curriculum.update(val_loss)
        if stage_changed:
            append_jsonl(FEED_PATH, {
                "ts": time.time(), "job": "training", "stage": "curriculum",
                "message": f"Advanced to curriculum stage {curriculum.current_stage}",
                "progress": round(0.10 + 0.85 * (epoch / _max_epochs), 4),
            })
            tb_writer.add_scalar("curriculum/stage_change", curriculum.current_stage, epoch)

        # Large judge evaluation [Gap #1]
        if large_judge and (epoch + 1) % judge_interval == 0:
            log.info("Running large judge evaluation (epoch %d)", epoch)
            try:
                large_judge.load()
                judge_samples = [dataset[i] for i in random.sample(range(len(dataset)),
                                 min(10, len(dataset)))]
                judgments = large_judge.score_batch(
                    judge_samples, config["large_judge"].get("protocols", ["rubric_scoring"]))
                avg_judge_score = sum(j.overall_score for j in judgments) / max(len(judgments), 1)
                tb_writer.add_scalar("judge/overall_score", avg_judge_score, epoch)
                log.info("Large judge avg score: %.2f", avg_judge_score)
                append_jsonl(FEED_PATH, {
                    "ts": time.time(),
                    "job": "training",
                    "stage": "judge_score",
                    "message": f"Epoch {epoch} reasoning score={avg_judge_score:.4f}",
                    "progress": round(progress, 4),
                    "epoch": epoch,
                })
                large_judge.unload()
            except Exception as exc:
                log.error("Large judge evaluation failed: %s", exc)
                if large_judge.is_loaded:
                    large_judge.unload()

        # Synthetic generation [Gap #4]
        if tc.get("synthetic_samples_per_epoch", 0) > 0:
            tokenizer_path = str(data_dir / "tokenizer.model")
            if Path(tokenizer_path).exists():
                synthetic = generate_synthetic_batch(
                    model, tokenizer_path, config, device,
                    count=tc["synthetic_samples_per_epoch"])
                if synthetic:
                    log.info("Generated %d synthetic samples", len(synthetic))

        # Save end-of-epoch checkpoint (with EMA weights as the "model_state"
        # for the best checkpoint — EMA produces superior final weights)
        ckpt_name = f"checkpoint_e{epoch}_final"
        save_checkpoint(
            CHECKPOINT_DIR / ckpt_name, epoch, global_step, model, critic_model,
            base_optimizer, critic_optimizer, scheduler,
            None, best_loss, curriculum.current_stage, 0, config, ema=ema,
            resume_next_epoch=True,
        )
        rotate_checkpoints(CHECKPOINT_DIR, keep_best=best_ckpt_count)
        last_checkpoint_name = ckpt_name
        last_checkpoint_epoch = epoch
        last_checkpoint_step = global_step
        last_checkpoint_ts = time.time()
        recovery_window_started_at = last_checkpoint_ts
        emit_recovery_point(round(0.10 + 0.85 * ((epoch + 1) / _max_epochs), 4), epoch, global_step, ckpt_name)
        persist_training_recovery_state(
            epoch=epoch,
            global_step=global_step,
            max_epochs=_max_epochs,
            checkpoint_every=ckpt_every,
            progress=0.10 + 0.85 * ((epoch + 1) / _max_epochs),
            last_checkpoint_name=last_checkpoint_name,
            last_checkpoint_epoch=last_checkpoint_epoch,
            last_checkpoint_step=last_checkpoint_step,
            last_checkpoint_ts=last_checkpoint_ts,
            recovery_window_started_at=recovery_window_started_at,
            avg_step_seconds=average_step_seconds,
            paused=False,
            active=True,
            message=f"Epoch {epoch} recovery point saved",
        )

    # Training complete
    tb_writer.close()
    append_jsonl(FEED_PATH, {
        "ts": time.time(), "job": "training", "stage": "completed",
        "message": f"Training complete: {tc['max_epochs']} epochs, best_loss={best_loss:.4f}",
        "progress": 1.0,
    })
    persist_training_recovery_state(
        epoch=max(0, _max_epochs - 1),
        global_step=global_step,
        max_epochs=_max_epochs,
        checkpoint_every=ckpt_every,
        progress=1.0,
        last_checkpoint_name=last_checkpoint_name,
        last_checkpoint_epoch=last_checkpoint_epoch,
        last_checkpoint_step=last_checkpoint_step,
        last_checkpoint_ts=last_checkpoint_ts,
        recovery_window_started_at=recovery_window_started_at,
        avg_step_seconds=average_step_seconds,
        paused=False,
        active=False,
        message="Training completed",
    )
    log.info("Training complete")


if __name__ == "__main__":
    # Write full tracebacks to a dedicated log file so they are captured even
    # when stderr is not piped by the parent process.
    _log_file = LOG_DIR / "training_error.log"
    try:
        ensure_dir(LOG_DIR)
        _fh = logging.FileHandler(str(_log_file), mode="a", encoding="utf-8")
        _fh.setLevel(logging.DEBUG)
        _fh.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s"))
        logging.getLogger().addHandler(_fh)
    except Exception:
        pass

    try:
        raise SystemExit(main())
    except Exception as exc:
        import traceback as _tb
        tb_text = _tb.format_exc()
        log.exception("Training failed: %s", exc)
        try:
            _log_file.write_text(
                f"\n{'='*60}\nTraining crashed: {exc}\n{tb_text}\n",
                encoding="utf-8",
            )
        except Exception:
            pass
        mark_training_failed(f"Training failed: {exc}")
        raise SystemExit(1)
