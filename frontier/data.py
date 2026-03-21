"""Dataset loading, scoring, threaded batch preparation, and quality filtering."""

import hashlib
import json
import logging
import math
import mmap
import os
import re
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

log = logging.getLogger(__name__)


def _encode_text(tokenizer, text: str) -> list[int]:
    """Encode text with a tokenizer exposing either encode() or Encode()."""
    encoder = getattr(tokenizer, "encode", None) or getattr(tokenizer, "Encode", None)
    if encoder is None:
        raise AttributeError("Tokenizer must define encode() or Encode()")
    return [int(token_id) for token_id in encoder(text)]


# ---------------------------------------------------------------------------
# Standard JSONL Dataset
# ---------------------------------------------------------------------------

class JsonlDataset:
    """PyTorch-compatible dataset wrapping a JSONL file."""

    def __init__(self, path, tokenizer=None, max_seq_len: int = 2048):
        self.path = Path(path)
        self.tokenizer = tokenizer
        self.max_seq_len = max_seq_len
        self.samples: list[dict] = []
        self._load()

    def _load(self):
        if not self.path.exists():
            log.warning("Dataset file not found: %s", self.path)
            return
        with open(self.path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    self.samples.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
        log.info("Loaded %d samples from %s", len(self.samples), self.path)

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        sample = self.samples[idx]
        if isinstance(sample.get("input_ids"), list):
            ids = [int(token_id) for token_id in sample["input_ids"][: self.max_seq_len]]
            return {"input_ids": ids, "score": float(sample.get("score", 0.5) or 0.5)}
        text = sample.get("text", sample.get("input", ""))
        if self.tokenizer is not None:
            ids = _encode_text(self.tokenizer, text)[: self.max_seq_len]
            return {"input_ids": ids, "score": float(sample.get("score", 0.5) or 0.5)}
        return sample


# ---------------------------------------------------------------------------
# Memory-Mapped JSONL Dataset [Gap #3]
# ---------------------------------------------------------------------------

class MemoryMappedJsonlDataset:
    """Memory-mapped JSONL dataset for large files — avoids full file load."""

    def __init__(self, path, tokenizer=None, max_seq_len: int = 2048):
        self.path = Path(path)
        self.tokenizer = tokenizer
        self.max_seq_len = max_seq_len
        self._offsets: list[int] = []
        self._mm = None
        self._file = None
        self._index()

    def _index(self):
        if not self.path.exists():
            return
        self._file = open(self.path, "rb")
        if self._file.seek(0, os.SEEK_END) == 0:
            self._file.seek(0)
            log.info("Indexed 0 lines from %s (mmap, empty file)", self.path)
            return
        self._file.seek(0)
        self._mm = mmap.mmap(self._file.fileno(), 0, access=mmap.ACCESS_READ)
        offset = 0
        while offset < self._mm.size():
            self._offsets.append(offset)
            nl = self._mm.find(b"\n", offset)
            if nl == -1:
                break
            offset = nl + 1
        log.info("Indexed %d lines from %s (mmap)", len(self._offsets), self.path)

    def __len__(self):
        return len(self._offsets)

    def __getitem__(self, idx):
        start = self._offsets[idx]
        end = self._offsets[idx + 1] if idx + 1 < len(self._offsets) else self._mm.size()
        raw = self._mm[start:end].decode("utf-8").strip()
        if not raw:
            return {"input_ids": [], "score": 0.0}
        sample = json.loads(raw)
        if isinstance(sample.get("input_ids"), list):
            ids = [int(token_id) for token_id in sample["input_ids"][: self.max_seq_len]]
            return {"input_ids": ids, "score": float(sample.get("score", 0.5) or 0.5)}
        text = sample.get("text", sample.get("input", ""))
        if self.tokenizer is not None:
            ids = _encode_text(self.tokenizer, text)[: self.max_seq_len]
            return {"input_ids": ids, "score": float(sample.get("score", 0.5) or 0.5)}
        return sample

    def close(self):
        mm_obj, self._mm = self._mm, None
        if mm_obj is not None:
            try:
                mm_obj.close()
            except (BufferError, OSError, ValueError):
                pass
        file_obj, self._file = self._file, None
        if file_obj is not None:
            try:
                file_obj.close()
            except OSError:
                pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        self.close()


# ---------------------------------------------------------------------------
# Source scoring helpers (parallelised with ThreadPoolExecutor)
# ---------------------------------------------------------------------------

def _entropy_score(text: str) -> float:
    """Simple character-level entropy heuristic."""
    if not text:
        return 0.0
    freq: dict[str, int] = {}
    for ch in text:
        freq[ch] = freq.get(ch, 0) + 1
    length = len(text)
    entropy = -sum((c / length) * math.log2(c / length) for c in freq.values() if c > 0)
    return min(entropy / 5.0, 1.0)  # normalise


def _reasoning_completeness(text: str) -> float:
    """Check for reasoning chain structure markers. [Gap #3]"""
    markers = ["<think>", "<reason>", "<verify>", "<proof>",
               "step 1", "step 2", "therefore", "because", "thus",
               "let's", "first", "next", "finally", "answer:"]
    found = sum(1 for m in markers if m.lower() in text.lower())
    return min(found / 5.0, 1.0)


def _answer_verification(sample: dict) -> float:
    """Verify answer presence and basic structure. [Gap #3]"""
    answer = sample.get("answer", sample.get("output", ""))
    if not answer:
        return 0.0
    score = 0.3  # has answer
    if len(answer.split()) > 3:
        score += 0.3  # non-trivial length
    if any(c.isdigit() for c in answer):
        score += 0.2  # contains numbers (helpful for math)
    if re.search(r"[.!?]$", answer.strip()):
        score += 0.2  # properly terminated
    return min(score, 1.0)


def _semantic_hash(text: str) -> str:
    """Quick content fingerprint for deduplication. [Gap #3]"""
    normalised = " ".join(text.lower().split())
    return hashlib.sha256(normalised.encode()).hexdigest()


def score_sample(sample: dict) -> float:
    """Compute a composite quality score for a single sample."""
    text = sample.get("text", sample.get("input", ""))
    entropy = _entropy_score(text)
    reasoning = _reasoning_completeness(text)
    answer = _answer_verification(sample)
    length_score = min(len(text.split()) / 200.0, 1.0)
    composite = 0.25 * entropy + 0.25 * reasoning + 0.25 * answer + 0.25 * length_score
    return round(composite, 4)


def score_sources(samples: list[dict], config: dict) -> list[dict]:
    """Score all samples in parallel using ThreadPoolExecutor."""
    workers = int(config.get("datasets", {}).get("source_workers", 4) or 4)
    threshold = float(config.get("datasets", {}).get("quality_threshold", 0.7) or 0.7)

    seen_hashes: set[str] = set()
    scored: list[dict] = []

    def _process(sample):
        text = sample.get("text", sample.get("input", ""))
        h = _semantic_hash(text)
        s = score_sample(sample)
        return sample, h, s

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = [pool.submit(_process, s) for s in samples]
        for future in as_completed(futures):
            sample, h, s = future.result()
            if h in seen_hashes:
                continue  # deduplicate
            seen_hashes.add(h)
            sample["score"] = s
            scored.append(sample)

    retained = [s for s in scored if float(s.get("score", 0) or 0) >= threshold]
    retained.sort(key=lambda x: float(x.get("score", 0) or 0), reverse=True)
    if not retained:
        # If no samples pass the threshold, keep the top 10% as fallback
        scored.sort(key=lambda x: float(x.get("score", 0) or 0), reverse=True)
        retained = scored[:max(1, len(scored) // 10)]
    log.info("Scored %d samples, retained %d above threshold %.2f",
             len(scored), len(retained), threshold)
    return retained


# ---------------------------------------------------------------------------
# Threaded batch loading
# ---------------------------------------------------------------------------

def hardware_aware_workers(config: dict) -> int:
    """Return worker count from config, bounded by CPU cores."""
    configured = int(config.get("training", {}).get("batch_item_workers", 4) or 4)
    return max(1, min(configured, (os.cpu_count() or 4)))


def threaded_batch_load(dataset, indices: list[int], workers: int = 4) -> list:
    """Load batch items in parallel threads."""
    results = [None] * len(indices)

    def _load(pos, idx):
        results[pos] = dataset[idx]

    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = []
        for pos, idx in enumerate(indices):
            futures.append(pool.submit(_load, pos, idx))
        for f in futures:
            f.result()  # wait and propagate exceptions
    return [r for r in results if r is not None]


_prefetch_result: dict[tuple[int, ...], list] = {}
_prefetch_lock = threading.Lock()
_PREFETCH_MAX_SIZE = 8


def prefetch_batch(dataset, indices: list[int], workers: int = 4) -> None:
    """Start background thread to prefetch the next batch."""
    if not indices:
        return
    key = tuple(indices)

    def _run():
        batch = threaded_batch_load(dataset, indices, workers)
        with _prefetch_lock:
            # Evict oldest entries if cache is full
            while len(_prefetch_result) >= _PREFETCH_MAX_SIZE:
                _prefetch_result.pop(next(iter(_prefetch_result)))
            _prefetch_result[key] = batch

    t = threading.Thread(target=_run, daemon=True)
    t.start()


def get_prefetched(indices: list[int]) -> list | None:
    """Retrieve a prefetched batch for *indices*, or None if not ready."""
    key = tuple(indices)
    with _prefetch_lock:
        return _prefetch_result.pop(key, None)
