from frontier.config import load_config, normalize_defaults, resolve_huggingface_token, save_config


def test_normalize_defaults_merges_values_and_coerces_numeric_strings():
    config = normalize_defaults(
        {
            "training": {
                "batch_size": "16",
                "learning_rate": "0.001",
                "best_checkpoint_count": "3",
            },
            "curriculum": {
                "stages": {
                    "2": {
                        "focus": "custom_focus",
                        "mix": {"reasoning": 1.0},
                    }
                }
            },
        }
    )

    assert config["training"]["batch_size"] == 16
    assert config["training"]["best_checkpoint_count"] == 3
    assert config["training"]["learning_rate"] == 0.001
    assert 2 in config["curriculum"]["stages"]
    assert config["curriculum"]["stages"][2]["focus"] == "custom_focus"
    assert config["datasets"]["cache_dir"] == "data/cache"
    assert config["huggingface"]["token"] == ""


def test_save_and_load_config_round_trip(tmp_path):
    config_path = tmp_path / "config.yaml"
    input_config = {
        "training": {"batch_size": 12},
        "datasets": {"sources": [{"name": "demo-source"}]},
    }

    save_config(input_config, config_path)
    loaded = load_config(config_path)

    assert loaded["training"]["batch_size"] == 12
    assert loaded["datasets"]["sources"] == [{"name": "demo-source"}]
    assert loaded["datasets"]["cache_dir"] == "data/cache"


def test_resolve_huggingface_token_prefers_config_then_environment(monkeypatch):
    monkeypatch.setenv("HF_TOKEN", "env-token")

    assert (
        resolve_huggingface_token({"huggingface": {"token": "  config-token  "}})
        == "config-token"
    )
    assert resolve_huggingface_token({}) == "env-token"

    monkeypatch.delenv("HF_TOKEN", raising=False)
    assert resolve_huggingface_token({}) is None
