import os

import torch

from frontier.utils import (
    append_jsonl,
    checkpoint_artifact_path,
    latest_checkpoint,
    recent_jsonl,
    safe_torch_load,
    sanitize_filename,
)


def test_checkpoint_artifact_path_handles_directory_and_file_inputs(tmp_path):
    checkpoint_dir = tmp_path / "run_001"
    checkpoint_dir.mkdir()
    checkpoint_file_in_dir = checkpoint_dir / "checkpoint.pt"
    checkpoint_file_in_dir.write_bytes(b"checkpoint")

    standalone_checkpoint = tmp_path / "model.bin"
    standalone_checkpoint.write_bytes(b"weights")

    non_checkpoint = tmp_path / "notes.txt"
    non_checkpoint.write_text("hello", encoding="utf-8")

    assert checkpoint_artifact_path(checkpoint_dir) == checkpoint_file_in_dir
    assert checkpoint_artifact_path(standalone_checkpoint) == standalone_checkpoint
    assert checkpoint_artifact_path(non_checkpoint) is None


def test_latest_checkpoint_returns_newest_checkpoint_artifact(tmp_path):
    older = tmp_path / "older_run"
    newer = tmp_path / "newer_run"
    older.mkdir()
    newer.mkdir()
    (older / "checkpoint.pt").write_bytes(b"old")
    (newer / "checkpoint.pt").write_bytes(b"new")

    os.utime(older, (1_700_000_000, 1_700_000_000))
    os.utime(newer, (1_800_000_000, 1_800_000_000))

    assert latest_checkpoint(tmp_path) == newer


def test_append_jsonl_and_recent_jsonl_return_latest_entries(tmp_path):
    log_path = tmp_path / "events.jsonl"

    append_jsonl(log_path, {"step": 1, "status": "started"})
    append_jsonl(log_path, {"step": 2, "status": "finished"})

    assert recent_jsonl(log_path, n=1) == [{"step": 2, "status": "finished"}]
    assert recent_jsonl(log_path, n=5) == [
        {"step": 1, "status": "started"},
        {"step": 2, "status": "finished"},
    ]


def test_safe_torch_load_wraps_raw_state_dict_payload(tmp_path):
    checkpoint_path = tmp_path / "raw_state.pt"
    raw_state = {"weight": torch.tensor([1.0, 2.0, 3.0])}
    torch.save(raw_state, checkpoint_path)

    loaded = safe_torch_load(checkpoint_path, map_location="cpu")

    assert "model_state" in loaded
    assert torch.equal(loaded["model_state"]["weight"], raw_state["weight"])


def test_sanitize_filename_replaces_reserved_characters():
    assert sanitize_filename('bad<name>:with|chars?.txt') == "bad_name__with_chars_.txt"
