"""Optimizer creation, LR scheduling, and CPU offload wrapper."""

import logging
import math

import torch
import torch.nn as nn

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Standard optimizer / scheduler creation
# ---------------------------------------------------------------------------

def create_optimizer(model: nn.Module, config: dict) -> torch.optim.Optimizer:
    """Create AdamW optimizer from config."""
    tc = config.get("training", config)
    lr = float(tc.get("learning_rate", 3e-4) or 3e-4)
    wd = float(tc.get("weight_decay", 0.01) or 0.01)

    # Separate weight-decay and no-decay groups
    decay_params = []
    no_decay_params = []
    for name, param in model.named_parameters():
        if not param.requires_grad:
            continue
        if "bias" in name or "norm" in name:
            no_decay_params.append(param)
        else:
            decay_params.append(param)

    groups = [
        {"params": decay_params, "weight_decay": wd},
        {"params": no_decay_params, "weight_decay": 0.0},
    ]
    optimizer = torch.optim.AdamW(groups, lr=lr, betas=(0.9, 0.95), eps=1e-8)
    log.info("Created AdamW optimizer: lr=%.2e, wd=%.4f", lr, wd)
    return optimizer


class CosineWarmupScheduler(torch.optim.lr_scheduler._LRScheduler):
    """Cosine annealing with linear warmup."""

    def __init__(self, optimizer, warmup_steps: int, total_steps: int, min_lr: float = 1e-6):
        self.warmup_steps = warmup_steps
        self.total_steps = max(total_steps, warmup_steps + 1)
        self.min_lr = min_lr
        super().__init__(optimizer, last_epoch=-1)

    def get_lr(self):
        step = self.last_epoch
        if step < self.warmup_steps:
            scale = step / max(self.warmup_steps, 1)
        else:
            progress = (step - self.warmup_steps) / max(self.total_steps - self.warmup_steps, 1)
            min_scale = self.min_lr / self.base_lrs[0] if self.base_lrs[0] > 0 else 0.0
            scale = max(min_scale, 0.5 * (1.0 + math.cos(math.pi * progress)))
        return [base_lr * scale for base_lr in self.base_lrs]


def create_scheduler(optimizer, config: dict, total_steps: int):
    """Create cosine-warmup LR scheduler from config."""
    tc = config.get("training", config)
    warmup = int(tc.get("warmup_steps", 500) or 500)
    # Default min_lr is 10% of peak LR (Chinchilla-style)
    lr = float(tc.get("learning_rate", 3e-4) or 3e-4)
    min_lr = float(tc.get("min_lr", lr * 0.1) or lr * 0.1)
    return CosineWarmupScheduler(optimizer, warmup, total_steps, min_lr=min_lr)


# ---------------------------------------------------------------------------
# CPU Offload Optimizer Wrapper [Gap #2, #6]
# ---------------------------------------------------------------------------

class CPUOffloadOptimizer(torch.optim.Optimizer):
    """Wraps a standard optimizer to keep state on CPU, reducing VRAM usage.

    Before each ``step()``, optimizer states are moved to GPU.
    After ``step()``, they are moved back to CPU.
    This saves ~40% VRAM on optimizer states for a 120M-param model.
    """

    def __init__(self, optimizer: torch.optim.Optimizer, device: torch.device | None = None):
        self.optimizer = optimizer
        self.device = device or (torch.device("cuda") if torch.cuda.is_available()
                                 else torch.device("cpu"))
        self._cpu = torch.device("cpu")
        self._initialized = False
        # Register as a real Optimizer instance so schedulers/scalers accept the wrapper.
        self._wrapping_init = True
        super().__init__(optimizer.param_groups, optimizer.defaults)
        self._wrapping_init = False
        self.defaults = optimizer.defaults
        self.state = optimizer.state
        self.param_groups = optimizer.param_groups

    def __getattr__(self, name):
        if name == "optimizer":
            raise AttributeError(name)
        return getattr(self.optimizer, name)

    def zero_grad(self, set_to_none: bool = True):
        self.optimizer.zero_grad(set_to_none=set_to_none)

    def add_param_group(self, param_group):
        if getattr(self, "_wrapping_init", False):
            return super().add_param_group(param_group)
        self.optimizer.add_param_group(param_group)
        self.param_groups = self.optimizer.param_groups
        self.state = self.optimizer.state

    def _offload_to_cpu(self):
        """Move optimizer state tensors to CPU."""
        for group in self.optimizer.param_groups:
            for p in group["params"]:
                state = self.optimizer.state.get(p)
                if state is None:
                    continue
                for key, val in state.items():
                    if isinstance(val, torch.Tensor) and val.device.type != "cpu":
                        state[key] = val.to(self._cpu, non_blocking=True)

    def _restore_to_device(self):
        """Move optimizer state tensors back to GPU."""
        for group in self.optimizer.param_groups:
            for p in group["params"]:
                state = self.optimizer.state.get(p)
                if state is None:
                    continue
                for key, val in state.items():
                    if isinstance(val, torch.Tensor) and val.device != self.device:
                        state[key] = val.to(self.device, non_blocking=True)

    def step(self, closure=None):
        """Perform optimizer step with CPU offload cycle."""
        # After first step, optimizer state gets populated
        if self._initialized:
            self._restore_to_device()
            if torch.cuda.is_available():
                torch.cuda.synchronize()

        result = self.optimizer.step(closure)

        if not self._initialized:
            self._initialized = True

        self._offload_to_cpu()
        if torch.cuda.is_available():
            torch.cuda.synchronize()

        return result

    def state_dict(self):
        return self.optimizer.state_dict()

    def load_state_dict(self, state_dict):
        self.optimizer.load_state_dict(state_dict)
        self.param_groups = self.optimizer.param_groups
        self.state = self.optimizer.state
        self._offload_to_cpu()
        self._initialized = True
