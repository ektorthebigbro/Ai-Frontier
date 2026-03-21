"""Hardware detection, VRAM monitoring, autotune, and memory ceiling enforcement."""

import logging
import os
import subprocess
from dataclasses import dataclass

import psutil

from frontier.cache import PROJECT_CACHE

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# GPU / CPU detection
# ---------------------------------------------------------------------------

def detect_gpu() -> dict:
    """Return GPU name, total VRAM (MB), and CUDA availability."""
    def _load() -> dict:
        info = {"name": "none", "vram_total_mb": 0, "cuda_available": False}
        try:
            import torch
            if torch.cuda.is_available():
                info["cuda_available"] = True
                info["name"] = torch.cuda.get_device_name(0)
                props = torch.cuda.get_device_properties(0)
                info["vram_total_mb"] = getattr(props, "total_memory", 0) // (1024 * 1024)
        except Exception as exc:
            log.warning("GPU detection failed: %s", exc)
        return info

    return dict(PROJECT_CACHE.get_or_set("hardware:detect_gpu", _load, ttl_seconds=10.0))


def detect_cpu() -> dict:
    """Return CPU info, core count, and total RAM."""
    def _load() -> dict:
        return {
            "cpu_count": os.cpu_count() or 1,
            "ram_total_mb": psutil.virtual_memory().total // (1024 * 1024),
            "ram_available_mb": psutil.virtual_memory().available // (1024 * 1024),
        }

    return dict(PROJECT_CACHE.get_or_set("hardware:detect_cpu", _load, ttl_seconds=5.0))


# ---------------------------------------------------------------------------
# Runtime VRAM queries
# ---------------------------------------------------------------------------

def get_vram_usage() -> dict:
    """Query current VRAM allocated and reserved (MB)."""
    def _load() -> dict:
        info = {"allocated_mb": 0, "reserved_mb": 0, "total_mb": 0, "free_mb": 0}
        try:
            import torch
            if torch.cuda.is_available():
                info["allocated_mb"] = torch.cuda.memory_allocated(0) // (1024 * 1024)
                info["reserved_mb"] = torch.cuda.memory_reserved(0) // (1024 * 1024)
                props = torch.cuda.get_device_properties(0)
                info["total_mb"] = getattr(props, "total_memory", 0) // (1024 * 1024)
                info["free_mb"] = info["total_mb"] - info["allocated_mb"]
        except Exception:
            pass
        return info

    return dict(PROJECT_CACHE.get_or_set("hardware:get_vram_usage", _load, ttl_seconds=0.75))


def nvidia_smi_status() -> dict | None:
    """Call nvidia-smi and parse utilization, memory, and temperature."""
    def _load() -> dict | None:
        try:
            result = subprocess.run(
                [
                    "nvidia-smi",
                    "--query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu",
                    "--format=csv,noheader,nounits",
                ],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if result.returncode != 0:
                return None
            parts = result.stdout.strip().split(",")
            if len(parts) < 4:
                return None
            return {
                "utilization_pct": int(parts[0].strip()),
                "memory_used_mb": int(parts[1].strip()),
                "memory_total_mb": int(parts[2].strip()),
                "temperature_c": int(parts[3].strip()),
            }
        except Exception:
            return None

    return PROJECT_CACHE.get_or_set("hardware:nvidia_smi_status", _load, ttl_seconds=1.0)


def clear_hardware_cache() -> None:
    """Invalidate all cached hardware probes."""
    PROJECT_CACHE.invalidate(prefix="hardware:")


# ---------------------------------------------------------------------------
# Hardware autotune
# ---------------------------------------------------------------------------

def autotune(config: dict) -> dict:
    """Set sensible worker/thread defaults based on detected hardware."""
    cpu_count = os.cpu_count() or 4
    gpu = detect_gpu()

    datasets_cfg = config.setdefault("datasets", {})
    training_cfg = config.setdefault("training", {})
    eval_cfg = config.setdefault("evaluation", {})

    # Workers: cap at half of CPU cores, minimum 2
    safe_workers = max(2, min(cpu_count // 2, 8))
    datasets_cfg.setdefault("source_workers", int(safe_workers))
    training_cfg.setdefault("batch_item_workers", int(safe_workers))
    training_cfg.setdefault("prefetch_batches", 1)
    eval_cfg.setdefault("cpu_threads", int(safe_workers))

    # VRAM-based micro-batch sizing
    if gpu["vram_total_mb"] > 0:
        vram_gb = gpu["vram_total_mb"] / 1024
        ceiling = float(training_cfg.get("vram_ceiling_gb", 5.5) or 5.5)
        if vram_gb <= ceiling + 0.5:
            training_cfg.setdefault("micro_batch_size", 1)
        elif vram_gb <= 8:
            training_cfg.setdefault("micro_batch_size", 2)
        else:
            training_cfg.setdefault("micro_batch_size", 4)

    log.info("Autotune: workers=%d, GPU=%s, VRAM=%dMB",
             safe_workers, gpu["name"], gpu["vram_total_mb"])
    return config


# ---------------------------------------------------------------------------
# VRAM Ceiling — hard 5.5 GB enforcement for RTX 3060 6 GB [Gap #2]
# ---------------------------------------------------------------------------

@dataclass
class VRAMCeiling:
    """Enforces a hard VRAM ceiling and provides adaptive batch control."""

    ceiling_gb: float = 5.5
    pressure_threshold: float = 0.90  # trigger batch shrink above 90%
    min_micro_batch: int = 1

    @property
    def ceiling_mb(self) -> int:
        return int(self.ceiling_gb * 1024)

    def check_pressure(self) -> float:
        """Return current VRAM utilization ratio (0.0 – 1.0)."""
        usage = get_vram_usage()
        if usage["total_mb"] == 0:
            return 0.0
        return usage["allocated_mb"] / min(self.ceiling_mb, usage["total_mb"])

    def should_shrink_batch(self) -> bool:
        """Return True if VRAM pressure exceeds the threshold."""
        return self.check_pressure() > self.pressure_threshold

    def recommend_batch_size(self, current: int) -> int:
        """Return reduced micro-batch size if under pressure, else current."""
        if not self.should_shrink_batch():
            return current
        new_size = max(self.min_micro_batch, current // 2)
        if new_size < current:
            log.warning("VRAM pressure: shrinking micro-batch %d -> %d", current, new_size)
        return new_size

    def try_clear_cache(self) -> None:
        """Release PyTorch CUDA cache if available."""
        try:
            import torch
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception:
            pass

    @staticmethod
    def offload_optimizer_states(optimizer, device_cpu=None):
        """Move all optimizer state tensors to CPU to free VRAM."""
        import torch
        cpu = device_cpu or torch.device("cpu")
        for group in optimizer.param_groups:
            for p in group["params"]:
                state = optimizer.state.get(p)
                if state is None:
                    continue
                for key, val in state.items():
                    if isinstance(val, torch.Tensor) and val.device.type != "cpu":
                        state[key] = val.to(cpu, non_blocking=True)

    @staticmethod
    def restore_optimizer_states(optimizer, device):
        """Move optimizer state tensors back to *device* before step."""
        import torch
        for group in optimizer.param_groups:
            for p in group["params"]:
                state = optimizer.state.get(p)
                if state is None:
                    continue
                for key, val in state.items():
                    if isinstance(val, torch.Tensor) and val.device != device:
                        state[key] = val.to(device, non_blocking=True)
