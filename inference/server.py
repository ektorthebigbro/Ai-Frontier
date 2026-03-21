"""Inference API server for checkpoint-based text generation."""

import argparse
import asyncio
import logging
import threading
from pathlib import Path
import sys

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

import torch
import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel

from frontier.config import load_config
from frontier.model_management import apply_default_model_presets
from frontier.modeling import FrontierTransformer
from frontier.utils import (
    checkpoint_artifact_path,
    latest_checkpoint,
    load_sentencepiece_tokenizer,
    project_root,
    safe_torch_load,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("inference")

app = FastAPI(title="AI Frontier Inference", version="2.4.1")

_model = None
_tokenizer = None
_device = None
_config = None
_loaded_checkpoint_path = ""
_model_lock = threading.RLock()
_generation_lock = threading.Lock()


class GenerateRequest(BaseModel):
    prompt: str
    max_new_tokens: int = 256
    temperature: float = 0.7
    top_k: int = 50
    top_p: float = 0.9
    checkpoint_path: str | None = None


class GenerateResponse(BaseModel):
    generated: str
    prompt: str
    tokens_generated: int
    checkpoint_name: str = ""
    checkpoint_path: str = ""


def _candidate_checkpoint_paths(checkpoint_path: str) -> list[Path]:
    raw_path = Path(checkpoint_path).expanduser()
    candidates = [raw_path]
    if not raw_path.is_absolute():
        candidates.append(project_root() / raw_path)
        candidates.append(project_root() / "checkpoints" / raw_path)

    unique: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate)
        if key in seen:
            continue
        seen.add(key)
        unique.append(candidate)
    return unique


def _resolve_checkpoint_path(checkpoint_path: str | None) -> Path | None:
    if checkpoint_path:
        for candidate in _candidate_checkpoint_paths(checkpoint_path):
            if candidate.exists():
                return candidate
        return _candidate_checkpoint_paths(checkpoint_path)[0]
    return latest_checkpoint(project_root() / "checkpoints")


def _normalize_checkpoint_path(path: Path | None) -> str:
    if path is None:
        return ""
    try:
        return str(path.resolve())
    except OSError:
        return str(path)


def _checkpoint_file_for(path: Path) -> Path:
    checkpoint_file = checkpoint_artifact_path(path)
    if checkpoint_file is not None:
        return checkpoint_file
    return path / "checkpoint.pt" if path.is_dir() else path


def _checkpoint_name(path: str) -> str:
    if not path:
        return ""
    checkpoint_path = Path(path)
    if checkpoint_path.name == "checkpoint.pt" and checkpoint_path.parent.name:
        return checkpoint_path.parent.name
    return checkpoint_path.name


def _validate_generate_request(req: GenerateRequest) -> None:
    if not req.prompt or not req.prompt.strip():
        raise HTTPException(status_code=422, detail="Prompt must not be empty")
    if req.max_new_tokens <= 0:
        raise HTTPException(status_code=422, detail="max_new_tokens must be positive")
    if req.temperature < 0:
        raise HTTPException(status_code=422, detail="temperature must be non-negative")
    if req.top_k < 0:
        raise HTTPException(status_code=422, detail="top_k must be non-negative")
    if not 0.0 < req.top_p <= 1.0:
        raise HTTPException(status_code=422, detail="top_p must be in the range (0, 1]")


def _encode_prompt(tokenizer, prompt: str, max_seq_len: int) -> list[int]:
    ids = tokenizer.encode(prompt)
    if not ids:
        raise HTTPException(status_code=422, detail="Prompt did not produce any tokens")
    if len(ids) > max_seq_len:
        log.warning(
            "Prompt exceeded max_seq_len=%d; truncating to the last %d tokens",
            max_seq_len,
            max_seq_len,
        )
        ids = ids[-max_seq_len:]
    return ids


def load_model(config: dict, checkpoint_path: str | None = None, *, strict_checkpoint: bool = False):
    """Load model and tokenizer into global state."""
    global _model, _tokenizer, _device, _config, _loaded_checkpoint_path

    with _model_lock:
        _config = config
        _device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        _model = FrontierTransformer(config).to(_device)
        _loaded_checkpoint_path = ""

        ckpt_path = _resolve_checkpoint_path(checkpoint_path)
        if ckpt_path:
            ckpt_file = _checkpoint_file_for(ckpt_path)
            if ckpt_file.exists():
                ckpt_data = safe_torch_load(ckpt_file, map_location=_device)
                _model.load_state_dict(ckpt_data["model_state"])
                if ckpt_data.get("ema_state") and ckpt_data["ema_state"].get("shadow"):
                    from frontier.modeling import ModelEMA

                    ema = ModelEMA(_model)
                    ema.load_state_dict(ckpt_data["ema_state"])
                    ema.apply(_model)
                    log.info("Applied EMA weights from checkpoint")
                _loaded_checkpoint_path = _normalize_checkpoint_path(ckpt_path)
                log.info("Loaded checkpoint: %s", ckpt_path)
            elif strict_checkpoint:
                raise FileNotFoundError(f"Requested checkpoint does not exist: {ckpt_file}")
            else:
                log.warning("Requested checkpoint does not exist: %s", ckpt_file)
        else:
            if strict_checkpoint:
                raise FileNotFoundError("No checkpoint found")
            log.warning("No checkpoint found - using randomly initialised model")

        _model.eval()

        data_dir = project_root() / config.get("datasets", {}).get("data_dir", "data/processed")
        tokenizer_path = data_dir / "tokenizer.model"
        try:
            _tokenizer = load_sentencepiece_tokenizer(tokenizer_path)
            if _tokenizer is None:
                raise FileNotFoundError(tokenizer_path)
            log.info("Loaded tokenizer: %s", tokenizer_path)
        except Exception as exc:
            log.warning("Could not load tokenizer: %s", exc)
            _tokenizer = None


@app.get("/health")
def health():
    return {
        "status": "ok",
        "model_loaded": _model is not None,
        "tokenizer_loaded": _tokenizer is not None,
        "device": str(_device),
        "checkpoint_path": _loaded_checkpoint_path,
        "checkpoint_name": _checkpoint_name(_loaded_checkpoint_path),
    }


@app.post("/generate", response_model=GenerateResponse)
def generate(req: GenerateRequest):
    _validate_generate_request(req)

    with _generation_lock:
        with _model_lock:
            if _model is None:
                raise HTTPException(status_code=503, detail="Model not loaded")
            if _tokenizer is None:
                raise HTTPException(status_code=503, detail="Tokenizer not loaded")

            requested_checkpoint = _resolve_checkpoint_path(req.checkpoint_path)
            if _normalize_checkpoint_path(requested_checkpoint) != _loaded_checkpoint_path:
                try:
                    load_model(_config, req.checkpoint_path, strict_checkpoint=True)
                except FileNotFoundError as exc:
                    raise HTTPException(status_code=404, detail=str(exc)) from exc
                except Exception as exc:
                    raise HTTPException(status_code=500, detail=f"Could not load requested checkpoint: {exc}") from exc

            model = _model
            tokenizer = _tokenizer
            device = _device
            loaded_checkpoint_path = _loaded_checkpoint_path

        ids = _encode_prompt(tokenizer, req.prompt, model.max_seq_len)
        input_ids = torch.tensor([ids], device=device)
        with torch.no_grad():
            output_ids = model.generate(
                input_ids,
                max_new_tokens=req.max_new_tokens,
                temperature=req.temperature,
                top_k=req.top_k,
                top_p=req.top_p,
                eos_token_id=tokenizer.eos_token_id,
            )

        new_ids = output_ids[0].tolist()[len(ids):]
        generated_text = tokenizer.decode(new_ids)
        return GenerateResponse(
            generated=generated_text,
            prompt=req.prompt,
            tokens_generated=len(new_ids),
            checkpoint_name=_checkpoint_name(loaded_checkpoint_path),
            checkpoint_path=loaded_checkpoint_path,
        )


@app.post("/generate/stream")
async def generate_stream(req: GenerateRequest):
    """Streaming generation endpoint that serializes model access."""
    _validate_generate_request(req)
    loop = asyncio.get_running_loop()
    await asyncio.to_thread(_generation_lock.acquire)

    try:
        with _model_lock:
            if _model is None or _tokenizer is None:
                raise HTTPException(status_code=503, detail="Model or tokenizer not loaded")

            requested_checkpoint = _resolve_checkpoint_path(req.checkpoint_path)
            if _normalize_checkpoint_path(requested_checkpoint) != _loaded_checkpoint_path:
                try:
                    load_model(_config, req.checkpoint_path, strict_checkpoint=True)
                except FileNotFoundError as exc:
                    raise HTTPException(status_code=404, detail=str(exc)) from exc
                except Exception as exc:
                    raise HTTPException(status_code=500, detail=f"Could not load requested checkpoint: {exc}") from exc

            model = _model
            tokenizer = _tokenizer
            device = _device

        ids = _encode_prompt(tokenizer, req.prompt, model.max_seq_len)
        eos_token_id = tokenizer.eos_token_id
    except Exception:
        _generation_lock.release()
        raise

    def _generate_one_token(input_ids_local):
        with torch.no_grad():
            logits = model(input_ids_local[:, -model.max_seq_len:])
            logits = logits[:, -1, :] / max(req.temperature, 1e-8)

            if req.top_k > 0:
                values, _ = torch.topk(logits, min(req.top_k, logits.size(-1)))
                logits[logits < values[:, [-1]]] = float("-inf")

            if req.top_p < 1.0:
                sorted_logits, sorted_idx = torch.sort(logits, descending=True)
                sorted_probs = torch.softmax(sorted_logits, dim=-1)
                cumulative = torch.cumsum(sorted_probs, dim=-1)
                remove = cumulative - sorted_probs >= req.top_p
                filtered = sorted_logits.masked_fill(remove, float("-inf"))
                logits = torch.full_like(logits, float("-inf")).scatter(1, sorted_idx, filtered)

            probs = torch.softmax(logits, dim=-1)
            next_token = torch.multinomial(probs, num_samples=1)
            return torch.cat([input_ids_local, next_token], dim=1), next_token.item()

    async def token_stream():
        try:
            input_ids = torch.tensor([ids], device=device)
            for _ in range(req.max_new_tokens):
                input_ids, token_id = await loop.run_in_executor(None, _generate_one_token, input_ids)
                yield tokenizer.decode([token_id])
                if token_id == eos_token_id:
                    break
        finally:
            _generation_lock.release()

    return StreamingResponse(token_stream(), media_type="text/plain")


def main():
    parser = argparse.ArgumentParser(description="AI Frontier: Inference Server")
    parser.add_argument("--config", default=str(project_root() / "configs" / "default.yaml"))
    parser.add_argument("--checkpoint", default=None)
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    args = parser.parse_args()

    config = apply_default_model_presets(load_config(args.config))
    inf_cfg = config.get("inference", {})
    host = args.host or inf_cfg.get("host", "127.0.0.1")
    port = args.port or inf_cfg.get("port", 8766)

    load_model(config, args.checkpoint)

    log.info("Starting inference server on %s:%d", host, port)
    uvicorn.run(app, host=host, port=port, log_level="info")


if __name__ == "__main__":
    main()
