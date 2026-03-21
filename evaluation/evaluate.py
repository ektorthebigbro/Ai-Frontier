"""Automated evaluation with benchmark reporting and protocol-based judgments."""

import argparse
import json
import logging
import re
import time
from pathlib import Path
import sys

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

import torch

from frontier.config import load_config
from frontier.hardware import autotune, detect_gpu
from frontier.judging import LargeJudge
from frontier.model_management import apply_default_model_presets
from frontier.modeling import FrontierTransformer, ModelEMA
from frontier.utils import (
    append_jsonl,
    ensure_dir,
    latest_checkpoint,
    load_sentencepiece_tokenizer,
    project_root,
    safe_torch_load,
    sanitize_filename,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
log = logging.getLogger("evaluation")

FEED_PATH = str(project_root() / "logs" / "dashboard_metrics.jsonl")


def mark_evaluation_failed(message: str, progress: float = 0.0) -> None:
    """Emit a terminal failure row for evaluation."""
    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "evaluate",
        "stage": "failed",
        "message": message,
        "progress": round(progress, 4),
    })


GSM8K_SAMPLES = [
    {
        "prompt": "Janet's ducks lay 16 eggs per day. She eats three for breakfast every morning and bakes muffins for her friends every day with four. She sells every remaining egg at the farmers' market for $2. How much does she make every day at the farmers' market?",
        "expected": "18",
    },
    {
        "prompt": "A robe takes 2 bolts of blue fiber and half that much white fiber. How many bolts in total does it take?",
        "expected": "3",
    },
    {
        "prompt": "Josh decides to try flipping a house. He buys a house for $80,000 and then puts in $50,000 in repairs. This increased the value of the house by 150%. How much profit did he make?",
        "expected": "70000",
    },
    {
        "prompt": "James decides to run 3 sprints 3 times a week. He runs 60 meters each sprint. How many total meters does he run a week?",
        "expected": "540",
    },
    {
        "prompt": "Every day, Wendi feeds each of her chickens three cups of mixed chicken feed, containing seeds, mealworms and vegetables to help keep them healthy. She gives the chickens their feed in three separate meals. In the morning, she gives her flock of chickens 15 cups of feed. In the afternoon, she gives her chickens another 25 cups of feed. If Wendi has 20 chickens, how many cups of feed does she need to give her chickens in the final meal of the day that evening?",
        "expected": "20",
    },
]

REASONING_PROBES = [
    {
        "prompt": "If all roses are flowers and some flowers fade quickly, can we conclude that some roses fade quickly? Explain your reasoning.",
        "type": "logic",
    },
    {
        "prompt": "A bat and a ball cost $1.10 in total. The bat costs $1.00 more than the ball. How much does the ball cost?",
        "type": "math",
    },
    {
        "prompt": "Continue the sequence: 2, 6, 12, 20, 30, ...",
        "type": "pattern",
    },
    {
        "prompt": "If it takes 5 machines 5 minutes to make 5 widgets, how long would it take 100 machines to make 100 widgets?",
        "type": "math",
    },
    {
        "prompt": "Write a function to check if a number is prime. Then verify it works for the number 17.",
        "type": "code",
    },
]


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


def resolve_checkpoint_path(checkpoint_path: str | None) -> Path | None:
    if checkpoint_path:
        for candidate in _candidate_checkpoint_paths(checkpoint_path):
            if candidate.exists():
                return candidate
        return _candidate_checkpoint_paths(checkpoint_path)[0]
    return latest_checkpoint(project_root() / "checkpoints")


def checkpoint_file_for(path: Path) -> Path:
    return path / "checkpoint.pt" if path.is_dir() else path


def extract_number(text: str) -> str | None:
    """Extract the last number from generated text."""
    numbers = re.findall(r"[\d,]+\.?\d*", text.replace(",", ""))
    return numbers[-1] if numbers else None


def run_gsm8k(model, tokenizer, device: torch.device, config: dict) -> dict:
    """Run GSM8K-style math evaluation."""
    correct = 0
    total = len(GSM8K_SAMPLES)
    results = []

    for sample in GSM8K_SAMPLES:
        prompt = f"Solve step by step: {sample['prompt']}\nAnswer: "
        ids = tokenizer.encode(prompt)
        input_ids = torch.tensor([ids], device=device)

        with torch.no_grad():
            output_ids = model.generate(
                input_ids,
                max_new_tokens=256,
                temperature=0.3,
                top_k=10,
                eos_token_id=tokenizer.eos_token_id,
            )
        generated = tokenizer.decode(output_ids[0].tolist()[len(ids):])
        extracted = extract_number(generated)
        is_correct = extracted is not None and extracted.strip() == sample["expected"].strip()
        if is_correct:
            correct += 1

        results.append({
            "prompt": sample["prompt"][:100],
            "expected": sample["expected"],
            "extracted": extracted,
            "correct": is_correct,
            "generated": generated[:200],
        })

    accuracy = correct / max(total, 1)
    log.info("GSM8K: %d/%d correct (%.1f%%)", correct, total, accuracy * 100)
    return {
        "benchmark": "gsm8k",
        "accuracy": accuracy,
        "correct": correct,
        "total": total,
        "results": results,
    }


def run_reasoning_probes(model, tokenizer, device: torch.device, config: dict) -> dict:
    """Run reasoning probe evaluation."""
    results = []

    for probe in REASONING_PROBES:
        prompt = f"Think step by step:\n{probe['prompt']}\n\nAnswer: "
        ids = tokenizer.encode(prompt)
        input_ids = torch.tensor([ids], device=device)

        with torch.no_grad():
            output_ids = model.generate(
                input_ids,
                max_new_tokens=256,
                temperature=0.5,
                top_k=30,
                eos_token_id=tokenizer.eos_token_id,
            )
        generated = tokenizer.decode(output_ids[0].tolist()[len(ids):])

        has_reasoning = any(marker in generated.lower() for marker in ["because", "therefore", "step", "first", "so"])
        has_answer = len(generated.strip()) > 10
        score = 0.5 * has_reasoning + 0.5 * has_answer

        results.append({
            "prompt": probe["prompt"][:100],
            "type": probe["type"],
            "score": score,
            "has_reasoning": has_reasoning,
            "generated": generated[:200],
        })

    avg_score = sum(result["score"] for result in results) / max(len(results), 1)
    log.info("Reasoning probes: avg_score=%.2f", avg_score)
    return {"benchmark": "reasoning_probes", "avg_score": avg_score, "results": results}


def run_protocol_evaluation(model, tokenizer, device: torch.device, config: dict) -> dict:
    """Run structured evaluation using intelligence protocols via the large judge."""
    judge_cfg = config.get("large_judge", {})
    if not judge_cfg.get("enabled", True):
        return {"benchmark": "protocol_eval", "skipped": True}

    try:
        judge = LargeJudge(
            judge_cfg["model_id"],
            str(project_root() / judge_cfg.get("cache_dir", "data/cache/large_judge")),
            device="cpu",
            trust_remote_code=judge_cfg.get("trust_remote_code", False),
        )
        judge.load()
    except Exception as exc:
        log.warning("Could not load large judge for protocol eval: %s", exc)
        return {"benchmark": "protocol_eval", "error": str(exc)}

    protocols = judge_cfg.get("protocols", ["rubric_scoring"])
    all_judgments = []

    try:
        for prompt in [probe["prompt"] for probe in REASONING_PROBES[:3]]:
            ids = tokenizer.encode(prompt)
            input_ids = torch.tensor([ids], device=device)
            with torch.no_grad():
                output_ids = model.generate(
                    input_ids,
                    max_new_tokens=256,
                    temperature=0.5,
                    eos_token_id=tokenizer.eos_token_id,
                )
            response = tokenizer.decode(output_ids[0].tolist()[len(ids):])

            for protocol in protocols:
                try:
                    judgment = judge.generate_judgment(prompt, response, protocol)
                    all_judgments.append({
                        "prompt": prompt[:100],
                        "protocol": protocol,
                        "judgment": judgment.to_dict(),
                    })
                except Exception as exc:
                    log.warning("Protocol eval failed for %s: %s", protocol, exc)
    finally:
        if judge.is_loaded:
            judge.unload()

    avg_overall = 0.0
    if all_judgments:
        scores = [item["judgment"]["overall_score"] for item in all_judgments]
        avg_overall = sum(scores) / len(scores)

    return {
        "benchmark": "protocol_eval",
        "avg_overall_score": avg_overall,
        "protocol_count": len(all_judgments),
        "judgments": all_judgments,
    }


def generate_report(checkpoint_id: str, results: list[dict], config: dict) -> dict:
    """Build a machine-readable evaluation report."""
    report = {
        "checkpoint_id": checkpoint_id,
        "timestamp": time.time(),
        "curriculum_stage": config.get("curriculum", {}).get("current_stage", 1),
        "benchmarks": {},
    }

    for result in results:
        report["benchmarks"][result.get("benchmark", "unknown")] = result

    report["gsm8k_accuracy"] = report["benchmarks"].get("gsm8k", {}).get("accuracy", 0.0)
    report["reasoning_score"] = report["benchmarks"].get("reasoning_probes", {}).get("avg_score", 0.0)
    report["protocol_overall_score"] = report["benchmarks"].get("protocol_eval", {}).get("avg_overall_score", 0.0)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="AI Frontier: Evaluation")
    parser.add_argument("--config", default=str(project_root() / "configs" / "default.yaml"))
    parser.add_argument("--checkpoint", default=None, help="Checkpoint path (default: latest)")
    args = parser.parse_args()

    config = apply_default_model_presets(load_config(args.config))
    config = autotune(config)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log.info("Device: %s, GPU: %s", device, detect_gpu())

    ensure_dir(project_root() / "logs")
    ensure_dir(project_root() / config.get("evaluation", {}).get("report_dir", "artifacts"))

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "evaluate",
        "stage": "starting",
        "message": "Evaluation starting",
        "progress": 0.0,
    })

    ckpt_path = resolve_checkpoint_path(args.checkpoint)
    if not ckpt_path or not ckpt_path.exists():
        log.error("No checkpoint found")
        mark_evaluation_failed("No checkpoint found", progress=0.0)
        return 1

    append_jsonl(FEED_PATH, {
        "ts": time.time(),
        "job": "evaluate",
        "stage": "loading",
        "message": f"Loading checkpoint: {ckpt_path.name}",
        "progress": 0.10,
    })

    try:
        model = FrontierTransformer(config).to(device)
        ckpt_file = checkpoint_file_for(ckpt_path)
        ckpt_data = safe_torch_load(ckpt_file, map_location=device)
        model.load_state_dict(ckpt_data["model_state"])
        if ckpt_data.get("ema_state") and ckpt_data["ema_state"].get("shadow"):
            ema = ModelEMA(model)
            ema.load_state_dict(ckpt_data["ema_state"])
            ema.apply(model)
            log.info("Applied EMA weights from checkpoint")
        model.eval()

        checkpoint_id = ckpt_path.name
        data_dir = project_root() / config["datasets"].get("data_dir", "data/processed")
        tokenizer_path = data_dir / "tokenizer.model"
        tokenizer = load_sentencepiece_tokenizer(tokenizer_path)
        if tokenizer is None:
            log.error("Failed to load tokenizer: %s", tokenizer_path)
            mark_evaluation_failed(f"Failed to load tokenizer: {tokenizer_path}", progress=0.10)
            return 1

        results = []

        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "evaluate",
            "stage": "gsm8k",
            "message": "Running GSM8K benchmark",
            "progress": 0.20,
        })
        results.append(run_gsm8k(model, tokenizer, device, config))

        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "evaluate",
            "stage": "reasoning",
            "message": "Running reasoning probes",
            "progress": 0.50,
        })
        results.append(run_reasoning_probes(model, tokenizer, device, config))

        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "evaluate",
            "stage": "protocols",
            "message": "Running protocol evaluation",
            "progress": 0.70,
        })
        results.append(run_protocol_evaluation(model, tokenizer, device, config))

        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "evaluate",
            "stage": "report",
            "message": "Generating evaluation report",
            "progress": 0.90,
        })

        report = generate_report(checkpoint_id, results, config)
        report_dir = ensure_dir(project_root() / config["evaluation"].get("report_dir", "artifacts"))
        report_path = report_dir / f"eval_report_{sanitize_filename(checkpoint_id)}_{int(time.time())}.json"
        with open(report_path, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, default=str)

        log.info("Report saved: %s", report_path)
        log.info("GSM8K accuracy: %.1f%%", report["gsm8k_accuracy"] * 100)
        log.info("Reasoning score: %.2f", report["reasoning_score"])
        log.info("Protocol score: %.2f", report["protocol_overall_score"])

        append_jsonl(FEED_PATH, {
            "ts": time.time(),
            "job": "evaluate",
            "stage": "completed",
            "message": f"Evaluation complete: GSM8K={report['gsm8k_accuracy']:.1%}",
            "progress": 1.0,
        })
        return 0
    except Exception as exc:
        log.exception("Evaluation failed: %s", exc)
        mark_evaluation_failed(f"Evaluation failed: {exc}", progress=0.90)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
