"""Dataset preparation entrypoint: tokenizer training, quality scoring, and dataset building."""

import argparse
import json
import logging
import re
import shutil
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from frontier.config import load_config, resolve_huggingface_token
from frontier.data import score_sample, score_sources
from frontier.hardware import autotune
from frontier.model_management import apply_default_model_presets, ensure_required_models
from frontier.utils import append_jsonl, ensure_dir, project_root

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("prepare")
logging.getLogger("httpx").setLevel(logging.WARNING)
logging.getLogger("httpcore").setLevel(logging.WARNING)

FEED_PATH = str(project_root() / "logs" / "dashboard_metrics.jsonl")


# ---------------------------------------------------------------------------
# Tokenizer training (SentencePiece)
# ---------------------------------------------------------------------------

def train_tokenizer(texts: list[str], config: dict, output_dir: Path):
    """Train a SentencePiece tokenizer on raw texts."""
    import sentencepiece as spm

    vocab_size = int(config.get("datasets", {}).get("tokenizer_vocab_size", 32000) or 32000)
    model_prefix = str(output_dir / "tokenizer")
    corpus_path = output_dir / "tokenizer_corpus.txt"

    log.info("Writing tokenizer corpus (%d texts)", len(texts))
    with open(corpus_path, "w", encoding="utf-8") as handle:
        for text in texts:
            cleaned = " ".join(text.split())
            if len(cleaned) > 10:
                handle.write(cleaned + "\n")

    training_args = {
        "input": str(corpus_path),
        "model_prefix": model_prefix,
        "vocab_size": vocab_size,
        "model_type": "bpe",
        "character_coverage": 0.9995,
        "num_threads": 4,
        "max_sentence_length": 8192,
        "shuffle_input_sentence": True,
        "pad_id": 0,
        "unk_id": 1,
        "bos_id": 2,
        "eos_id": 3,
        "hard_vocab_limit": False,
    }

    log.info("Training SentencePiece tokenizer (target_vocab_size=%d)", vocab_size)
    try:
        spm.SentencePieceTrainer.train(**training_args)
    except RuntimeError as exc:
        match = re.search(r"value <=\s*(\d+)", str(exc))
        if not match:
            raise
        fallback_vocab = int(match.group(1))
        log.warning(
            "Tokenizer corpus cannot support vocab_size=%d; retrying with fallback_vocab_size=%d",
            vocab_size,
            fallback_vocab,
        )
        training_args["vocab_size"] = fallback_vocab
        training_args["hard_vocab_limit"] = True
        spm.SentencePieceTrainer.train(**training_args)

    processor = spm.SentencePieceProcessor()
    processor.load(model_prefix + ".model")
    actual_vocab_size = processor.get_piece_size()
    if actual_vocab_size != vocab_size:
        log.warning(
            "Tokenizer realized vocab_size=%d from requested vocab_size=%d based on available corpus",
            actual_vocab_size,
            vocab_size,
        )
    log.info("Tokenizer saved to %s", model_prefix)
    return model_prefix + ".model"


# ---------------------------------------------------------------------------
# Source loading from HuggingFace datasets
# ---------------------------------------------------------------------------

def emit_prepare_progress(stage: str, message: str, progress: float):
    print(f"AI_PROGRESS|prepare|{stage}|{progress:.4f}|{message}", flush=True)
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": stage,
        "message": message,
        "progress": round(progress, 4),
    })


def mark_prepare_failed(message: str, progress: float = 0.0) -> None:
    """Emit a terminal failure row for dataset preparation."""
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "failed",
        "message": message,
        "progress": round(progress, 4),
    })


def _cache_tokens(name: str, subset: str | None) -> set[str]:
    tokens: set[str] = set()
    for raw in [name, subset or "", name.split("/")[-1], name.split("/")[0]]:
        token = re.sub(r"[^A-Za-z0-9]+", "_", raw or "").strip("_").lower()
        if token:
            tokens.add(token)
    return tokens


def normalize_dataset_source_name(name: str) -> str:
    aliases = {
        "Open-Orca/open_orca": "Open-Orca/OpenOrca",
        "gsm8k": "openai/gsm8k",
    }
    normalized = aliases.get(name, name)
    if normalized != name:
        log.info("Normalizing dataset source %s -> %s", name, normalized)
    return normalized


def source_snapshot_path(config: dict, name: str, subset: str | None) -> Path:
    snapshot_dir = ensure_dir(
        project_root() / config.get("datasets", {}).get("data_dir", "data/processed") / "source_snapshots"
    )
    source_key = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_")
    subset_key = re.sub(r"[^A-Za-z0-9]+", "_", subset or "default").strip("_")
    return snapshot_dir / f"{source_key}__{subset_key}.jsonl"


def source_snapshot_status_path(config: dict, name: str, subset: str | None) -> Path:
    snapshot = source_snapshot_path(config, name, subset)
    return snapshot.with_suffix(".status.json")


def atomic_write_text(path: Path, content: str) -> None:
    ensure_dir(path.parent)
    temp_path = path.with_suffix(path.suffix + ".tmp")
    with open(temp_path, "w", encoding="utf-8") as handle:
        handle.write(content)
    temp_path.replace(path)


def write_source_snapshot(
    config: dict,
    name: str,
    subset: str | None,
    samples: list[dict],
    *,
    allow_empty: bool = False,
) -> Path | None:
    if not samples and not allow_empty:
        return None
    snapshot = source_snapshot_path(config, name, subset)
    lines = [json.dumps(sample, default=str) for sample in samples]
    atomic_write_text(snapshot, "\n".join(lines) + ("\n" if lines else ""))
    return snapshot


def write_source_snapshot_status(
    config: dict,
    name: str,
    subset: str | None,
    *,
    status: str,
    sample_count: int,
    sample_limit: int,
    snapshot: Path | None,
    message: str,
) -> Path:
    status_path = source_snapshot_status_path(config, name, subset)
    payload = {
        "source": name,
        "subset": subset or "default",
        "status": status,
        "sample_count": sample_count,
        "sample_limit": sample_limit,
        "progress": round(sample_count / max(sample_limit, 1), 4),
        "snapshot_path": str(snapshot) if snapshot is not None else "",
        "message": message,
        "updated_at": time.time(),
    }
    atomic_write_text(status_path, json.dumps(payload, indent=2, default=str) + "\n")
    return status_path


def repair_source_cache(cache_dir: Path, name: str, subset: str | None) -> int:
    """Delete likely-corrupted cache fragments for a specific dataset source."""
    tokens = _cache_tokens(name, subset)
    removed = 0
    if not cache_dir.exists():
        return removed

    for path in sorted(cache_dir.rglob("*"), key=lambda item: len(str(item)), reverse=True):
        lower_name = path.name.lower()
        if not any(token in lower_name for token in tokens):
            continue
        if path.is_file() and path.suffix.lower() in {".lock", ".tmp", ".incomplete"}:
            try:
                path.unlink()
                removed += 1
            except OSError:
                pass
        elif path.is_dir() and path.name.startswith("data_cache_"):
            try:
                shutil.rmtree(path)
                removed += 1
            except OSError:
                pass
    return removed


def validate_dataset_sources(config: dict) -> tuple[bool, str]:
    """Validate dataset source configuration before expensive prepare steps run."""
    datasets_cfg = config.get("datasets", {})
    sources_cfg = datasets_cfg.get("sources", [])
    if not isinstance(sources_cfg, list) or not sources_cfg:
        return False, "No dataset sources configured; set datasets.sources before prepare"

    valid_sources = 0
    for source in sources_cfg:
        if isinstance(source, str) and source.strip():
            valid_sources += 1
            continue
        if isinstance(source, dict) and str(source.get("name", "")).strip():
            valid_sources += 1

    if valid_sources <= 0:
        return False, "Dataset sources are present but invalid; each source needs a non-empty name"

    return True, ""


def load_hf_sources(config: dict) -> list[dict]:
    """Download and extract text samples from configured HF dataset sources."""
    from datasets import load_dataset

    sources_cfg = config.get("datasets", {}).get("sources", [])
    max_samples = int(config.get("datasets", {}).get("max_samples", 100000) or 100000)
    cache_dir = project_root() / config.get("datasets", {}).get("cache_dir", "data/cache")
    trust_remote_code = config.get("datasets", {}).get("trust_remote_code", False)
    hf_token = resolve_huggingface_token(config)
    all_samples: list[dict] = []
    total_sources = max(len(sources_cfg), 1)

    for idx, source in enumerate(sources_cfg):
        raw_name = source.get("name", source) if isinstance(source, dict) else source
        name = normalize_dataset_source_name(raw_name)
        subset = source.get("subset", None) if isinstance(source, dict) else None
        split = source.get("split", "train") if isinstance(source, dict) else "train"
        weight = float(source.get("weight", 1.0) or 1.0) if isinstance(source, dict) else 1.0
        per_source_limit = int(max_samples * weight / max(len(sources_cfg), 1))
        source_start = 0.15 + 0.20 * (idx / total_sources)
        source_end = 0.15 + 0.20 * ((idx + 1) / total_sources)

        if per_source_limit <= 0:
            log.info("Skipping source: %s (computed limit=%d)", name, per_source_limit)
            emit_prepare_progress("dataset_source", f"Skipping source {name} (limit=0)", source_end)
            continue

        log.info("Loading source: %s (subset=%s, limit=%d)", name, subset, per_source_limit)
        emit_prepare_progress("dataset_source", f"Loading source {name}", source_start)
        source_snapshot = write_source_snapshot(config, name, subset, [], allow_empty=True)
        write_source_snapshot_status(
            config,
            name,
            subset,
            status="starting",
            sample_count=0,
            sample_limit=per_source_limit,
            snapshot=source_snapshot,
            message=f"Preparing source snapshot for {name}",
        )

        dataset = None
        for attempt in range(2):
            try:
                dataset = load_dataset(
                    name,
                    subset,
                    split=split,
                    cache_dir=str(cache_dir),
                    trust_remote_code=trust_remote_code,
                    streaming=False,
                    token=hf_token,
                )
                break
            except Exception as exc:
                if attempt == 0:
                    repaired = repair_source_cache(cache_dir, name, subset)
                    emit_prepare_progress(
                        "dataset_repair",
                        f"Repairing cache for {name} after load failure ({repaired} items cleared)",
                        min(source_start + 0.02, source_end),
                    )
                    write_source_snapshot_status(
                        config,
                        name,
                        subset,
                        status="repairing",
                        sample_count=0,
                        sample_limit=per_source_limit,
                        snapshot=source_snapshot,
                        message=f"Repairing cached fragments for {name} after a load failure",
                    )
                    log.warning("Retrying %s after cache repair: %s", name, exc)
                    continue
                log.error("Failed to load %s after repair retry: %s", name, exc)
                emit_prepare_progress("dataset_error", f"Skipping {name} after repeated load failure", source_end)
                write_source_snapshot_status(
                    config,
                    name,
                    subset,
                    status="failed",
                    sample_count=0,
                    sample_limit=per_source_limit,
                    snapshot=source_snapshot,
                    message=f"Failed to load {name}: {exc}",
                )
                dataset = None

        if dataset is None:
            continue

        count = 0
        source_samples: list[dict] = []
        progress_tick = max(1, per_source_limit // 4)
        for row in dataset:
            if count >= per_source_limit:
                break
            text = row.get("text", row.get("question", row.get("input", "")))
            answer = row.get("answer", row.get("output", row.get("response", "")))
            if not text:
                continue
            sample = {"text": text, "source": name}
            if answer:
                sample["answer"] = answer
                sample["output"] = answer
            all_samples.append(sample)
            source_samples.append(sample)
            count += 1

            if count % progress_tick == 0 or count == per_source_limit:
                fraction = min(1.0, count / max(per_source_limit, 1))
                progress = source_start + (source_end - source_start) * fraction
                source_snapshot = write_source_snapshot(config, name, subset, source_samples, allow_empty=True)
                write_source_snapshot_status(
                    config,
                    name,
                    subset,
                    status="loading",
                    sample_count=count,
                    sample_limit=per_source_limit,
                    snapshot=source_snapshot,
                    message=f"Checkpointed {count} samples from {name}",
                )
                emit_prepare_progress(
                    "dataset_source",
                    f"Loaded {count}/{per_source_limit} samples from {name}",
                    progress,
                )

        log.info("Loaded %d samples from %s", count, name)
        snapshot = write_source_snapshot(config, name, subset, source_samples, allow_empty=True)
        if snapshot is not None:
            log.info("Saved %d source samples to %s", len(source_samples), snapshot)
        write_source_snapshot_status(
            config,
            name,
            subset,
            status="completed" if count > 0 else "empty",
            sample_count=count,
            sample_limit=per_source_limit,
            snapshot=snapshot,
            message=f"Finished loading {count} samples from {name}",
        )
        emit_prepare_progress("dataset_source", f"Loaded {count} samples from {name}", source_end)

    return all_samples


# ---------------------------------------------------------------------------
# Synthetic data generation [Gap #4]
# ---------------------------------------------------------------------------

def generate_synthetic_samples(samples: list[dict], config: dict) -> list[dict]:
    """Generate structured synthetic reasoning samples from existing data.

    Produces <think>/<reason>/<verify>/<proof> formatted reasoning chains.
    """
    synthetic: list[dict] = []
    count = config.get("training", {}).get("synthetic_samples_per_epoch", 50)

    for sample in samples[:count]:
        text = sample.get("text", "")
        answer = sample.get("answer", sample.get("output", ""))
        if not text or not answer:
            continue

        synthetic_text = (
            f"<think>\nAnalyze the following problem: {text}\n</think>\n"
            f"<reason>\nLet me work through this step by step:\n"
            f"Step 1: Identify the key elements of the problem.\n"
            f"Step 2: Apply relevant knowledge and reasoning.\n"
            f"Step 3: Derive the answer.\n</reason>\n"
            f"<verify>\nVerifying: Does the answer address the question completely? "
            f"Cross-checking against the problem statement.\n</verify>\n"
            f"<proof>\nThe answer is: {answer}\n</proof>"
        )

        synthetic.append({
            "text": synthetic_text,
            "source": "synthetic",
            "answer": answer,
            "output": answer,
            "score": score_sample({"text": synthetic_text, "answer": answer}),
        })

    log.info("Generated %d synthetic samples", len(synthetic))
    return synthetic


# ---------------------------------------------------------------------------
# Main entrypoint
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="AI Frontier: Dataset Preparation")
    parser.add_argument("--config", default=str(project_root() / "configs" / "default.yaml"))
    parser.add_argument("--dry-run", action="store_true", help="Show config and exit")
    args = parser.parse_args()

    config = apply_default_model_presets(load_config(args.config))
    config = autotune(config)

    if args.dry_run:
        log.info("Dry run - config loaded successfully")
        log.info(
            "Model IDs required: %s",
            [config["large_judge"]["model_id"]] + config["large_judge"].get("fallback_model_ids", []),
        )
        return 0

    ensure_dir(project_root() / "logs")
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "starting",
        "message": "Dataset preparation starting",
        "progress": 0.0,
    })

    sources_ok, sources_message = validate_dataset_sources(config)
    if not sources_ok:
        log.error("%s", sources_message)
        mark_prepare_failed(sources_message, progress=0.0)
        return 1

    ensure_required_models(config, FEED_PATH, job="prepare", progress_start=0.04, progress_end=0.14)

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "loading",
        "message": "Loading HuggingFace sources",
        "progress": 0.15,
    })
    raw_samples = load_hf_sources(config)
    log.info("Loaded %d raw samples total", len(raw_samples))

    if not raw_samples:
        failure_message = "No samples loaded from configured dataset sources"
        log.error("%s - aborting", failure_message)
        mark_prepare_failed(failure_message, progress=0.15)
        return 1

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "scoring",
        "message": "Scoring and filtering samples",
        "progress": 0.40,
    })
    scored = score_sources(raw_samples, config)
    if not scored:
        failure_message = "No samples remained after scoring/filtering"
        log.error("%s - aborting", failure_message)
        mark_prepare_failed(failure_message, progress=0.40)
        return 1

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "synthetic",
        "message": "Generating synthetic reasoning samples",
        "progress": 0.60,
    })
    synthetic = generate_synthetic_samples(scored, config)
    scored.extend(synthetic)
    texts = [sample.get("text", "") for sample in scored if sample.get("text")]
    if not texts:
        failure_message = "No text samples remained for tokenizer training"
        log.error("%s - aborting", failure_message)
        mark_prepare_failed(failure_message, progress=0.60)
        return 1

    output_dir = ensure_dir(project_root() / config.get("datasets", {}).get("data_dir", "data/processed"))
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "tokenizer",
        "message": "Training SentencePiece tokenizer",
        "progress": 0.70,
    })
    tokenizer_path = train_tokenizer(texts, config, output_dir)

    output_path = output_dir / "train_scored.jsonl"
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "writing",
        "message": f"Writing {len(scored)} samples to {output_path}",
        "progress": 0.90,
    })
    with open(output_path, "w", encoding="utf-8") as handle:
        for sample in scored:
            handle.write(json.dumps(sample, default=str) + "\n")

    log.info("Wrote %d scored samples to %s", len(scored), output_path)
    log.info("Tokenizer: %s", tokenizer_path)

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "prepare",
        "stage": "completed",
        "message": f"Preparation complete: {len(scored)} samples",
        "progress": 1.0,
    })
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        log.exception("Dataset preparation failed: %s", exc)
        mark_prepare_failed(f"Dataset preparation failed: {exc}", progress=0.90)
        raise SystemExit(1)
