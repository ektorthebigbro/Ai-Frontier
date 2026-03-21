import frontier.model_management as model_management


def test_apply_default_model_presets_is_non_mutating():
    original = {}

    updated = model_management.apply_default_model_presets(original)

    assert original == {}
    assert updated["large_judge"]["enabled"] is True
    assert updated["large_judge"]["model_id"] == model_management.default_model_id()
    assert updated["large_judge"]["fallback_model_ids"]


def test_required_model_ids_respects_enabled_flag_and_fallbacks():
    disabled = {"large_judge": {"enabled": False}}
    enabled = {
        "large_judge": {
            "enabled": True,
            "model_id": "primary/model",
            "fallback_model_ids": ["fallback/a", "fallback/b"],
        }
    }

    assert model_management.required_model_ids(disabled) == []
    assert model_management.required_model_ids(enabled) == [
        "primary/model",
        "fallback/a",
        "fallback/b",
    ]


def test_summarize_model_cache_detects_cached_model_contents(tmp_path, monkeypatch):
    cache_dir = tmp_path / "data" / "cache" / "large_judge"
    model_dir = cache_dir / "models--Qwen--Qwen2.5-3B-Instruct" / "snapshots" / "abc123"
    model_dir.mkdir(parents=True)
    (model_dir / "model.safetensors").write_bytes(b"weights")
    (model_dir / "tokenizer.model").write_text("tokenizer", encoding="utf-8")

    monkeypatch.setattr(model_management, "project_root", lambda: tmp_path)

    summary = model_management.summarize_model_cache(
        {"large_judge": {"cache_dir": "data/cache/large_judge"}}
    )

    cached = summary["large_judge"]["Qwen/Qwen2.5-3B-Instruct"]
    assert cached["cached"] is True
    assert cached["has_model"] is True
    assert cached["has_tokenizer"] is True
    assert "models--Qwen--Qwen2.5-3B-Instruct" in cached["path"]
