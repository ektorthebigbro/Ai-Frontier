from pathlib import Path

from fastapi.testclient import TestClient
import torch

import inference.server as server


class DummyTokenizer:
    eos_token_id = 99

    def encode(self, text: str) -> list[int]:
        return [11, 12, 13] if text.strip() else []

    def decode(self, token_ids: list[int]) -> str:
        return "|".join(str(token_id) for token_id in token_ids)


class DummyModel:
    max_seq_len = 6

    def generate(self, input_ids, **_kwargs):
        extra_tokens = torch.tensor([[21, 22]], device=input_ids.device)
        return torch.cat([input_ids, extra_tokens], dim=1)


def _set_loaded_state():
    server._model = DummyModel()
    server._tokenizer = DummyTokenizer()
    server._device = torch.device("cpu")
    server._config = {"inference": {"port": 8766}}
    server._loaded_checkpoint_path = server._normalize_checkpoint_path(
        Path("checkpoints") / "demo_run" / "checkpoint.pt"
    )


def test_health_endpoint_reports_loaded_state():
    _set_loaded_state()
    client = TestClient(server.app)

    response = client.get("/health")

    assert response.status_code == 200
    payload = response.json()
    assert payload["status"] == "ok"
    assert payload["model_loaded"] is True
    assert payload["tokenizer_loaded"] is True
    assert payload["device"] == "cpu"
    assert payload["checkpoint_name"] == "demo_run"


def test_generate_endpoint_returns_decoded_text(monkeypatch):
    _set_loaded_state()
    loaded_path = Path(server._loaded_checkpoint_path)
    monkeypatch.setattr(server, "_resolve_checkpoint_path", lambda checkpoint_path: loaded_path)

    client = TestClient(server.app)
    response = client.post(
        "/generate",
        json={
            "prompt": "hello world",
            "max_new_tokens": 2,
            "temperature": 0.8,
            "top_k": 10,
            "top_p": 0.95,
        },
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["generated"] == "21|22"
    assert payload["prompt"] == "hello world"
    assert payload["tokens_generated"] == 2
    assert payload["checkpoint_name"] == "demo_run"


def test_generate_endpoint_rejects_empty_prompt():
    _set_loaded_state()
    client = TestClient(server.app)

    response = client.post("/generate", json={"prompt": "   "})

    assert response.status_code == 422
    assert response.json()["detail"] == "Prompt must not be empty"


def test_generate_endpoint_returns_service_unavailable_when_model_missing():
    server._model = None
    server._tokenizer = DummyTokenizer()
    server._device = torch.device("cpu")
    server._config = {}
    server._loaded_checkpoint_path = ""
    client = TestClient(server.app)

    response = client.post("/generate", json={"prompt": "hello"})

    assert response.status_code == 503
    assert response.json()["detail"] == "Model not loaded"


def test_generate_endpoint_returns_not_found_for_missing_requested_checkpoint(monkeypatch):
    _set_loaded_state()
    current_path = Path(server._loaded_checkpoint_path)
    missing_path = Path("checkpoints") / "missing_run" / "checkpoint.pt"

    def fake_resolve(checkpoint_path):
        return missing_path if checkpoint_path else current_path

    def fake_load_model(_config, checkpoint_path, *, strict_checkpoint=False):
        raise FileNotFoundError(f"Requested checkpoint does not exist: {checkpoint_path}")

    monkeypatch.setattr(server, "_resolve_checkpoint_path", fake_resolve)
    monkeypatch.setattr(server, "load_model", fake_load_model)

    client = TestClient(server.app)
    response = client.post(
        "/generate",
        json={"prompt": "hello", "checkpoint_path": "missing_run/checkpoint.pt"},
    )

    assert response.status_code == 404
    assert "Requested checkpoint does not exist" in response.json()["detail"]
