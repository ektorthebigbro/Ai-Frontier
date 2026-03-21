"""Critic scoring, large judge integration, and 10 intelligence protocols."""

import json
import logging
import re
from dataclasses import dataclass, field

import torch
import torch.nn.functional as F

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Protocol data structures [Gap #1]
# ---------------------------------------------------------------------------

@dataclass
class FlawEntry:
    """A categorised flaw found during evaluation."""
    category: str  # logical_error, factual_error, missing_step, hallucination, etc.
    description: str
    severity: str = "medium"  # low, medium, high, critical


@dataclass
class ComparisonResult:
    """A/B comparison schema."""
    response_a_summary: str = ""
    response_b_summary: str = ""
    winner: str = ""  # "A", "B", or "tie"
    rationale: str = ""
    margin: float = 0.0


@dataclass
class ProtocolJudgment:
    """Complete structured judgment across all 10 intelligence protocols."""
    # 1. Task decomposition
    task_decomposition: list[str] = field(default_factory=list)
    # 2. Rubric-anchored 1-10 scoring
    rubric_scores: dict[str, int] = field(default_factory=lambda: {
        "coherence": 0, "accuracy": 0, "completeness": 0, "reasoning": 0,
        "creativity": 0, "instruction_following": 0, "safety": 0,
        "depth": 0, "clarity": 0, "efficiency": 0
    })
    # 3. Flaw taxonomy
    flaw_taxonomy: list[FlawEntry] = field(default_factory=list)
    # 4. Confidence calibration
    confidence: float = 0.5
    # 5. Chosen/rejected/reason triplet
    chosen: str = ""
    rejected: str = ""
    rejection_reason: str = ""
    # 6. Adversarial counter-argument
    adversarial_counter: str = ""
    # 7. A/B comparison
    comparison: ComparisonResult = field(default_factory=ComparisonResult)
    # 8. Reasoning chain evaluation
    reasoning_chain_eval: str = ""
    # 9. Overall score (weighted aggregate)
    overall_score: float = 0.0
    # 10. Structured feedback
    structured_feedback: str = ""

    def compute_overall(self) -> float:
        """Compute weighted aggregate from rubric scores."""
        weights = {
            "coherence": 0.10, "accuracy": 0.15, "completeness": 0.10,
            "reasoning": 0.20, "creativity": 0.05, "instruction_following": 0.15,
            "safety": 0.05, "depth": 0.05, "clarity": 0.10, "efficiency": 0.05,
        }
        total = sum(self.rubric_scores.get(k, 0) * w for k, w in weights.items())
        self.overall_score = round(total, 2)
        return self.overall_score

    def to_dict(self) -> dict:
        return {
            "task_decomposition": self.task_decomposition,
            "rubric_scores": self.rubric_scores,
            "flaw_taxonomy": [
                {"category": f.category, "description": f.description, "severity": f.severity}
                for f in self.flaw_taxonomy
            ],
            "confidence": self.confidence,
            "chosen": self.chosen,
            "rejected": self.rejected,
            "rejection_reason": self.rejection_reason,
            "adversarial_counter": self.adversarial_counter,
            "comparison": {
                "response_a_summary": self.comparison.response_a_summary,
                "response_b_summary": self.comparison.response_b_summary,
                "winner": self.comparison.winner,
                "rationale": self.comparison.rationale,
                "margin": self.comparison.margin,
            },
            "reasoning_chain_eval": self.reasoning_chain_eval,
            "overall_score": self.overall_score,
            "structured_feedback": self.structured_feedback,
        }


# ---------------------------------------------------------------------------
# Protocol prompt builders [Gap #1]
# ---------------------------------------------------------------------------

PROTOCOL_RUBRIC = (
    "Score the following response on each axis from 1 (worst) to 10 (best):\n"
    "Axes: coherence, accuracy, completeness, reasoning, creativity, "
    "instruction_following, safety, depth, clarity, efficiency.\n"
    "For each axis provide: <axis>: <score>/10 - <brief justification>\n"
    "Then provide:\n"
    "FLAWS: list any flaws with category (logical_error|factual_error|missing_step|"
    "hallucination|redundancy|off_topic) and severity (low|medium|high|critical)\n"
    "CONFIDENCE: your confidence in this judgment (0.0-1.0)\n"
    "COUNTER: the strongest argument against your verdict\n"
    "FEEDBACK: actionable improvement guidance\n"
)

PROTOCOL_COMPARISON = (
    "Compare Response A and Response B to the given prompt.\n"
    "Provide:\n"
    "WINNER: A or B or tie\n"
    "MARGIN: how much better (0.0-1.0)\n"
    "RATIONALE: why this response is better\n"
    "CHOSEN: the full text of the better response\n"
    "REJECTED: the full text of the worse response\n"
    "REASON: why the rejected response was worse\n"
)

PROTOCOL_REASONING = (
    "Evaluate the reasoning chain in the response.\n"
    "Check for: <think>, <reason>, <verify>, <proof> structure.\n"
    "Evaluate: logical flow, completeness of steps, verification quality.\n"
    "Score the reasoning chain quality from 1-10.\n"
)


def format_protocol_prompt(protocol_type: str, prompt: str,
                           response: str, response_b: str | None = None) -> str:
    """Build the full judge prompt for a specific protocol."""
    header = {
        "rubric_scoring": PROTOCOL_RUBRIC,
        "flaw_taxonomy": PROTOCOL_RUBRIC,  # same prompt covers both
        "chosen_rejected": PROTOCOL_COMPARISON,
        "confidence_calibration": PROTOCOL_RUBRIC,
        "adversarial_counter": PROTOCOL_RUBRIC,
        "comparison": PROTOCOL_COMPARISON,
        "reasoning_chain": PROTOCOL_REASONING,
    }.get(protocol_type, PROTOCOL_RUBRIC)

    parts = [header, f"\n---\nPROMPT:\n{prompt}\n\nRESPONSE A:\n{response}\n"]
    if response_b is not None:
        parts.append(f"\nRESPONSE B:\n{response_b}\n")
    return "".join(parts)


def parse_protocol_response(raw_text: str, protocol_type: str) -> ProtocolJudgment:
    """Extract structured fields from judge output text."""
    judgment = ProtocolJudgment()

    # Parse rubric scores (pattern: "axis: N/10")
    for axis in judgment.rubric_scores:
        pattern = rf"{axis}\s*:\s*(\d+)\s*/\s*10"
        match = re.search(pattern, raw_text, re.IGNORECASE)
        if match:
            judgment.rubric_scores[axis] = min(10, max(1, int(match.group(1))))

    # Parse flaws
    flaw_pattern = r"(logical_error|factual_error|missing_step|hallucination|redundancy|off_topic)\s*[:\-]\s*(.+?)(?:\s*\((low|medium|high|critical)\))?"
    for match in re.finditer(flaw_pattern, raw_text, re.IGNORECASE):
        judgment.flaw_taxonomy.append(
            FlawEntry(category=match.group(1).lower(),
                      description=match.group(2).strip(),
                      severity=(match.group(3) or "medium").lower())
        )

    # Parse confidence
    conf_match = re.search(r"CONFIDENCE\s*:\s*([\d.]+)", raw_text, re.IGNORECASE)
    if conf_match:
        judgment.confidence = min(1.0, max(0.0, float(conf_match.group(1))))

    # Parse counter-argument
    counter_match = re.search(r"COUNTER\s*:\s*(.+?)(?:\n|$)", raw_text, re.IGNORECASE)
    if counter_match:
        judgment.adversarial_counter = counter_match.group(1).strip()

    # Parse feedback
    fb_match = re.search(r"FEEDBACK\s*:\s*(.+?)(?:\n\n|$)", raw_text, re.IGNORECASE | re.DOTALL)
    if fb_match:
        judgment.structured_feedback = fb_match.group(1).strip()

    # Parse comparison fields
    winner_match = re.search(r"WINNER\s*:\s*(A|B|tie)", raw_text, re.IGNORECASE)
    if winner_match:
        judgment.comparison.winner = winner_match.group(1).upper()

    margin_match = re.search(r"MARGIN\s*:\s*([\d.]+)", raw_text, re.IGNORECASE)
    if margin_match:
        judgment.comparison.margin = min(1.0, float(margin_match.group(1)))

    chosen_match = re.search(r"CHOSEN\s*:\s*(.+?)(?:\nREJECTED|$)", raw_text, re.IGNORECASE | re.DOTALL)
    if chosen_match:
        judgment.chosen = chosen_match.group(1).strip()

    rejected_match = re.search(r"REJECTED\s*:\s*(.+?)(?:\nREASON|$)", raw_text, re.IGNORECASE | re.DOTALL)
    if rejected_match:
        judgment.rejected = rejected_match.group(1).strip()

    reason_match = re.search(r"REASON\s*:\s*(.+?)(?:\n\n|$)", raw_text, re.IGNORECASE | re.DOTALL)
    if reason_match:
        judgment.rejection_reason = reason_match.group(1).strip()

    # Parse reasoning chain eval
    rc_match = re.search(r"reasoning.*?:\s*(\d+)\s*/\s*10", raw_text, re.IGNORECASE)
    if rc_match:
        judgment.reasoning_chain_eval = f"Score: {rc_match.group(1)}/10"

    judgment.compute_overall()
    return judgment


# ---------------------------------------------------------------------------
# Critic scorer (fast in-loop, uses the trainable CriticModel)
# ---------------------------------------------------------------------------

class CriticScorer:
    """Wraps the trainable CriticModel for quick in-loop reward scoring."""

    def __init__(self, critic_model, device=None):
        self.model = critic_model
        self.device = device or (torch.device("cuda") if torch.cuda.is_available()
                                 else torch.device("cpu"))
        self.model.to(self.device)

    @torch.no_grad()
    def score(self, input_ids: torch.Tensor) -> torch.Tensor:
        """Return scalar reward for each sequence in the batch."""
        self.model.eval()
        input_ids = input_ids.to(self.device)
        rewards = self.model(input_ids)
        return rewards

    def train_step(self, input_ids: torch.Tensor, target_rewards: torch.Tensor,
                   optimizer: torch.optim.Optimizer) -> float:
        """One training step for the critic."""
        self.model.train()
        input_ids = input_ids.to(self.device)
        target_rewards = target_rewards.to(self.device)
        predicted = self.model(input_ids)
        loss = F.mse_loss(predicted, target_rewards)
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()
        return loss.item()


# ---------------------------------------------------------------------------
# Large judge (frozen pre-trained model for periodic evaluation)
# ---------------------------------------------------------------------------

class LargeJudge:
    """Loads a frozen pre-trained model for stronger periodic evaluation."""

    def __init__(self, model_id: str, cache_dir: str, device: str = "cpu",
                 trust_remote_code: bool = False):
        self.model_id = model_id
        self.cache_dir = cache_dir
        self.device = device
        self.trust_remote_code = trust_remote_code
        self._model = None
        self._tokenizer = None

    def load(self):
        """Load model and tokenizer from cache or HF hub."""
        try:
            from transformers import AutoModelForCausalLM, AutoTokenizer
            log.info("Loading large judge: %s", self.model_id)
            self._tokenizer = AutoTokenizer.from_pretrained(
                self.model_id, cache_dir=self.cache_dir,
                trust_remote_code=self.trust_remote_code)
            self._model = AutoModelForCausalLM.from_pretrained(
                self.model_id, cache_dir=self.cache_dir,
                trust_remote_code=self.trust_remote_code,
                torch_dtype=torch.float16 if self.device != "cpu" else torch.float32)
            self._model.to(self.device)
            self._model.eval()
            log.info("Large judge loaded on %s", self.device)
        except Exception as exc:
            log.error("Failed to load large judge %s: %s", self.model_id, exc)
            raise

    def unload(self):
        """Free model from memory."""
        del self._model
        del self._tokenizer
        self._model = None
        self._tokenizer = None
        if torch.cuda.is_available():
            torch.cuda.empty_cache()
        log.info("Large judge unloaded")

    @property
    def is_loaded(self) -> bool:
        return self._model is not None

    @torch.no_grad()
    def generate_judgment(self, prompt: str, response: str,
                          protocol_type: str = "rubric_scoring",
                          response_b: str | None = None,
                          max_new_tokens: int = 512) -> ProtocolJudgment:
        """Generate a structured protocol judgment for a prompt-response pair."""
        if not self.is_loaded:
            self.load()

        judge_prompt = format_protocol_prompt(protocol_type, prompt, response, response_b)
        inputs = self._tokenizer(judge_prompt, return_tensors="pt",
                                 truncation=True, max_length=2048).to(self.device)
        outputs = self._model.generate(
            **inputs, max_new_tokens=max_new_tokens,
            temperature=0.3, do_sample=True, top_p=0.9)
        raw_text = self._tokenizer.decode(outputs[0][inputs["input_ids"].shape[1]:],
                                          skip_special_tokens=True)
        return parse_protocol_response(raw_text, protocol_type)

    @torch.no_grad()
    def score_batch(self, samples: list[dict],
                    protocols: list[str] | None = None) -> list[ProtocolJudgment]:
        """Score a batch of samples across specified protocols."""
        if protocols is None:
            protocols = ["rubric_scoring"]
        results = []
        for sample in samples:
            prompt = sample.get("prompt", sample.get("input", ""))
            response = sample.get("response", sample.get("output", ""))
            # Use first protocol for batch scoring
            judgment = self.generate_judgment(prompt, response, protocols[0])
            results.append(judgment)
        return results
