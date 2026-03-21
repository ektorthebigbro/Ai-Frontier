"""Generator and critic transformer models for AI Frontier."""

import logging
import math

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.checkpoint import checkpoint as torch_checkpoint

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Positional encoding
# ---------------------------------------------------------------------------

class RotaryEmbedding(nn.Module):
    """Rotary positional embedding (RoPE)."""

    def __init__(self, dim: int, max_seq_len: int = 2048, base: float = 10000.0):
        super().__init__()
        inv_freq = 1.0 / (base ** (torch.arange(0, dim, 2).float() / dim))
        self.register_buffer("inv_freq", inv_freq, persistent=False)
        t = torch.arange(max_seq_len).float()
        freqs = torch.einsum("i,j->ij", t, inv_freq)
        emb = torch.cat([freqs, freqs], dim=-1)
        self.register_buffer("cos_cached", emb.cos(), persistent=False)
        self.register_buffer("sin_cached", emb.sin(), persistent=False)

    def forward(self, seq_len: int, offset: int = 0):
        return (self.cos_cached[offset:offset + seq_len],
                self.sin_cached[offset:offset + seq_len])


def _rotate_half(x):
    x1, x2 = x.chunk(2, dim=-1)
    return torch.cat((-x2, x1), dim=-1)


def apply_rotary_emb(q, k, cos, sin):
    q_embed = q * cos + _rotate_half(q) * sin
    k_embed = k * cos + _rotate_half(k) * sin
    return q_embed, k_embed


# ---------------------------------------------------------------------------
# Transformer blocks
# ---------------------------------------------------------------------------

class MultiHeadAttention(nn.Module):
    def __init__(self, d_model: int, n_heads: int, dropout: float = 0.1):
        super().__init__()
        assert d_model % n_heads == 0
        self.d_model = d_model
        self.n_heads = n_heads
        self.head_dim = d_model // n_heads
        self.qkv = nn.Linear(d_model, 3 * d_model, bias=False)
        self.out_proj = nn.Linear(d_model, d_model, bias=False)
        self.attn_dropout = dropout

    def forward(self, x, cos, sin, mask=None, kv_cache=None):
        B, T, C = x.shape
        qkv = self.qkv(x).reshape(B, T, 3, self.n_heads, self.head_dim)
        qkv = qkv.permute(2, 0, 3, 1, 4)  # (3, B, H, T, D)
        q, k, v = qkv[0], qkv[1], qkv[2]

        # Apply rotary embeddings
        cos = cos.unsqueeze(0).unsqueeze(0)  # (1, 1, T, D)
        sin = sin.unsqueeze(0).unsqueeze(0)
        q, k = apply_rotary_emb(q, k, cos, sin)

        # KV-cache for fast autoregressive generation
        if kv_cache is not None:
            if "k" in kv_cache:
                k = torch.cat([kv_cache["k"], k], dim=2)
                v = torch.cat([kv_cache["v"], v], dim=2)
            kv_cache["k"] = k
            kv_cache["v"] = v

        # Use PyTorch SDPA (Flash Attention when available)
        dropout_p = self.attn_dropout if self.training else 0.0
        out = F.scaled_dot_product_attention(
            q, k, v, attn_mask=mask, dropout_p=dropout_p, is_causal=(mask is None),
        )
        out = out.transpose(1, 2).reshape(B, T, C)
        return self.out_proj(out)


def _swiglu_hidden_dim(d_model: int) -> int:
    """Compute SwiGLU hidden size: (8/3)*d_model rounded to nearest multiple of 128."""
    raw = int(d_model * 8 / 3)
    return ((raw + 127) // 128) * 128


class FeedForward(nn.Module):
    def __init__(self, d_model: int, dropout: float = 0.1, hidden_dim: int | None = None):
        super().__init__()
        hidden = hidden_dim or _swiglu_hidden_dim(d_model)
        self.w1 = nn.Linear(d_model, hidden, bias=False)
        self.w2 = nn.Linear(hidden, d_model, bias=False)
        self.w3 = nn.Linear(d_model, hidden, bias=False)  # SwiGLU gate
        self.dropout = nn.Dropout(dropout)

    def forward(self, x):
        return self.dropout(self.w2(F.silu(self.w1(x)) * self.w3(x)))


class TransformerBlock(nn.Module):
    def __init__(self, d_model: int, n_heads: int, dropout: float = 0.1):
        super().__init__()
        self.attn_norm = nn.RMSNorm(d_model)
        self.attn = MultiHeadAttention(d_model, n_heads, dropout)
        self.ff_norm = nn.RMSNorm(d_model)
        self.ff = FeedForward(d_model, dropout)

    def forward(self, x, cos, sin, mask=None, kv_cache=None):
        x = x + self.attn(self.attn_norm(x), cos, sin, mask, kv_cache)
        x = x + self.ff(self.ff_norm(x))
        return x


# ---------------------------------------------------------------------------
# Generator model (~100M parameters)
# ---------------------------------------------------------------------------

class FrontierTransformer(nn.Module):
    """Decoder-only transformer for text generation."""

    def __init__(self, config: dict):
        super().__init__()
        mc = config.get("model", config)
        self.d_model = mc.get("d_model", 768)
        self.n_heads = mc.get("n_heads", 12)
        self.n_layers = mc.get("n_layers", 12)
        self.vocab_size = mc.get("vocab_size", 32000)
        self.max_seq_len = mc.get("max_seq_len", 2048)
        self.gradient_checkpointing = mc.get("gradient_checkpointing", True)
        dropout = mc.get("dropout", 0.05)

        self.embed = nn.Embedding(self.vocab_size, self.d_model)
        self.embed_scale = math.sqrt(self.d_model)
        self.rope = RotaryEmbedding(self.d_model // self.n_heads, self.max_seq_len)
        self.layers = nn.ModuleList([
            TransformerBlock(self.d_model, self.n_heads, dropout)
            for _ in range(self.n_layers)
        ])
        self.norm = nn.RMSNorm(self.d_model)
        self.lm_head = nn.Linear(self.d_model, self.vocab_size, bias=False)

        # Weight tying
        self.lm_head.weight = self.embed.weight

        self._init_weights()
        self._log_params()

    def _init_weights(self):
        """GPT-2 style init with scaled residual projections."""
        std = 0.02
        residual_std = std / math.sqrt(2.0 * self.n_layers)
        for name, module in self.named_modules():
            if isinstance(module, nn.Linear):
                # Scale down residual projections (out_proj and w2)
                if name.endswith("out_proj") or name.endswith("w2"):
                    nn.init.normal_(module.weight, mean=0.0, std=residual_std)
                else:
                    nn.init.normal_(module.weight, mean=0.0, std=std)
            elif isinstance(module, nn.Embedding):
                nn.init.normal_(module.weight, mean=0.0, std=std)

    def _log_params(self):
        total = sum(p.numel() for p in self.parameters())
        trainable = sum(p.numel() for p in self.parameters() if p.requires_grad)
        log.info("FrontierTransformer: %.1fM total params, %.1fM trainable",
                 total / 1_000_000, trainable / 1_000_000)

    def forward(self, input_ids, attention_mask=None):
        B, T = input_ids.shape
        x = self.embed(input_ids) * self.embed_scale
        cos, sin = self.rope(T)

        # For SDPA: pass explicit mask only when we have padding, otherwise use is_causal
        sdpa_mask = None
        if attention_mask is not None:
            # Combine causal + padding: (B, 1, T, T)
            causal = torch.tril(torch.ones(T, T, device=input_ids.device, dtype=torch.bool))
            pad_mask = attention_mask.bool().unsqueeze(1).unsqueeze(2)  # (B, 1, 1, T)
            sdpa_mask = causal.unsqueeze(0) & pad_mask

        for layer in self.layers:
            if self.gradient_checkpointing and self.training:
                x = torch_checkpoint(layer, x, cos, sin, sdpa_mask, None,
                                     use_reentrant=False)
            else:
                x = layer(x, cos, sin, sdpa_mask)

        x = self.norm(x)
        logits = self.lm_head(x)
        return logits

    def enable_activation_recomputation(self, layers: list[int] | None = None):
        """Selectively enable gradient checkpointing on specific layers."""
        if layers is None:
            self.gradient_checkpointing = True
            return
        self.gradient_checkpointing = False
        for i, layer in enumerate(self.layers):
            layer._use_checkpoint = i in layers

    @torch.no_grad()
    def generate(self, input_ids, max_new_tokens: int = 256,
                 temperature: float = 0.7, top_k: int = 50, top_p: float = 0.9,
                 eos_token_id: int | None = None):
        """Autoregressive generation with KV-cache for speed."""
        was_training = self.training
        self.eval()
        try:
            # Initialize KV-caches (one per layer)
            kv_caches = [{} for _ in self.layers]
            seq_len = input_ids.shape[1]

            # Prefill: process the full prompt
            x = self.embed(input_ids) * self.embed_scale
            cos, sin = self.rope(seq_len)
            for layer, kv in zip(self.layers, kv_caches):
                x = layer(x, cos, sin, kv_cache=kv)
            x = self.norm(x)
            logits = self.lm_head(x[:, -1:, :]).squeeze(1)

            generated = input_ids
            for step in range(max_new_tokens):
                logits = logits / max(temperature, 1e-8)

                if top_k > 0:
                    v, _ = torch.topk(logits, min(top_k, logits.size(-1)))
                    logits[logits < v[:, [-1]]] = float("-inf")

                if top_p < 1.0:
                    sorted_logits, sorted_idx = torch.sort(logits, descending=True)
                    sorted_probs = F.softmax(sorted_logits, dim=-1)
                    cumulative = torch.cumsum(sorted_probs, dim=-1)
                    remove = cumulative - sorted_probs >= top_p
                    filtered = sorted_logits.masked_fill(remove, float("-inf"))
                    logits = torch.full_like(logits, float("-inf")).scatter(
                        1, sorted_idx, filtered)

                probs = F.softmax(logits, dim=-1)
                next_token = torch.multinomial(probs, num_samples=1)
                generated = torch.cat([generated, next_token], dim=1)

                if eos_token_id is not None and bool(
                        torch.all(next_token.eq(eos_token_id))):
                    break

                # Decode step: only process the new token with cached KVs
                cur_pos = seq_len + step
                if cur_pos >= self.max_seq_len:
                    break
                x = self.embed(next_token) * self.embed_scale
                cos, sin = self.rope(1, offset=cur_pos)
                for layer, kv in zip(self.layers, kv_caches):
                    x = layer(x, cos, sin, kv_cache=kv)
                x = self.norm(x)
                logits = self.lm_head(x).squeeze(1)

            return generated
        finally:
            if was_training:
                self.train()


# ---------------------------------------------------------------------------
# EMA (Exponential Moving Average) for better final model quality
# ---------------------------------------------------------------------------

class ModelEMA:
    """Maintains an exponential moving average of model parameters.

    The EMA model typically produces better results than the raw trained model.
    Use ema.state_dict() when saving the final/best checkpoint.
    """

    def __init__(self, model: nn.Module, decay: float = 0.999):
        self.decay = decay
        self.shadow = {name: param.clone().detach()
                       for name, param in model.named_parameters() if param.requires_grad}
        self.num_updates = 0

    def update(self, model: nn.Module):
        """Update shadow parameters with current model parameters."""
        self.num_updates += 1
        # Warmup decay: start low and ramp up
        decay = min(self.decay, (1 + self.num_updates) / (10 + self.num_updates))
        with torch.no_grad():
            for name, param in model.named_parameters():
                if param.requires_grad and name in self.shadow:
                    self.shadow[name].lerp_(param.data, 1.0 - decay)

    def apply(self, model: nn.Module):
        """Copy shadow parameters into the model (for evaluation/saving)."""
        with torch.no_grad():
            for name, param in model.named_parameters():
                if name in self.shadow:
                    param.data.copy_(self.shadow[name])

    def state_dict(self) -> dict:
        return {"shadow": {k: v.clone() for k, v in self.shadow.items()},
                "num_updates": self.num_updates, "decay": self.decay}

    def load_state_dict(self, state_dict: dict):
        self.shadow = {k: v.clone() for k, v in state_dict["shadow"].items()}
        self.num_updates = state_dict.get("num_updates", 0)
        self.decay = state_dict.get("decay", self.decay)


# ---------------------------------------------------------------------------
# Critic model (smaller, outputs scalar reward)
# ---------------------------------------------------------------------------

class CriticModel(nn.Module):
    """Small transformer that outputs a scalar reward per sequence."""

    def __init__(self, config: dict):
        super().__init__()
        cc = config.get("critic", config)
        self.d_model = cc.get("d_model", 256)
        self.n_heads = cc.get("n_heads", 4)
        self.n_layers = cc.get("n_layers", 4)
        vocab_size = config.get("model", {}).get("vocab_size", cc.get("vocab_size", 32000))
        max_seq_len = config.get("model", {}).get("max_seq_len", cc.get("max_seq_len", 2048))
        dropout = cc.get("dropout", 0.1)

        self.embed = nn.Embedding(vocab_size, self.d_model)
        self.rope = RotaryEmbedding(self.d_model // self.n_heads, max_seq_len)
        self.layers = nn.ModuleList([
            TransformerBlock(self.d_model, self.n_heads, dropout)
            for _ in range(self.n_layers)
        ])
        self.norm = nn.RMSNorm(self.d_model)
        self.reward_head = nn.Linear(self.d_model, 1, bias=False)

        self._init_weights()
        total = sum(p.numel() for p in self.parameters())
        log.info("CriticModel: %.1fM params", total / 1_000_000)

    def _init_weights(self):
        for module in self.modules():
            if isinstance(module, nn.Linear):
                nn.init.normal_(module.weight, mean=0.0, std=0.02)
            elif isinstance(module, nn.Embedding):
                nn.init.normal_(module.weight, mean=0.0, std=0.02)

    def forward(self, input_ids, attention_mask=None):
        B, T = input_ids.shape
        x = self.embed(input_ids)
        cos, sin = self.rope(T)
        # Use SDPA with is_causal=True (no explicit mask needed for causal)
        sdpa_mask = None
        if attention_mask is not None:
            causal = torch.tril(torch.ones(T, T, device=input_ids.device, dtype=torch.bool))
            pad_mask = attention_mask.bool().unsqueeze(1).unsqueeze(2)
            sdpa_mask = causal.unsqueeze(0) & pad_mask
        for layer in self.layers:
            x = layer(x, cos, sin, sdpa_mask)
        x = self.norm(x)
        # Pool over last token
        reward = self.reward_head(x[:, -1, :]).squeeze(-1)
        return reward
