"""Tests for scripts/launcher.py argument parsing and config helpers."""

import sys
from pathlib import Path
from unittest.mock import patch

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT))

# Import after path setup
from scripts.launcher import (
    build_parser,
    load_config_value,
    backend_port,
    inference_port,
    detect_gpu_vendor,
    venv_python,
    on_windows,
)


class TestBuildParser:
    def test_setup_command_parses(self):
        parser = build_parser()
        args = parser.parse_args(["setup"])
        assert args.command == "setup"

    def test_build_command_requires_target(self):
        parser = build_parser()
        with pytest.raises(SystemExit):
            parser.parse_args(["build"])

    def test_build_command_accepts_valid_targets(self):
        parser = build_parser()
        for target in ("backend", "widgets", "qml"):
            args = parser.parse_args(["build", target])
            assert args.target == target

    def test_server_command_parses(self):
        parser = build_parser()
        args = parser.parse_args(["server"])
        assert args.command == "server"
        assert not args.skip_build

    def test_server_skip_build_flag(self):
        parser = build_parser()
        args = parser.parse_args(["server", "--skip-build"])
        assert args.skip_build

    def test_action_command_parses(self):
        parser = build_parser()
        args = parser.parse_args(["action", "training"])
        assert args.command == "action"
        assert args.name == "training"

    def test_action_command_requires_name(self):
        parser = build_parser()
        with pytest.raises(SystemExit):
            parser.parse_args(["action"])

    def test_run_command_parses(self):
        parser = build_parser()
        args = parser.parse_args(["run", "qml"])
        assert args.command == "run"
        assert args.target == "qml"


class TestConfigHelpers:
    def test_load_config_value_returns_default_when_no_config(self, tmp_path):
        with patch("scripts.launcher.CONFIG_PATH", tmp_path / "nonexistent.yaml"):
            assert load_config_value("dashboard", "port", default=9999) == 9999

    def test_backend_port_returns_default(self):
        with patch("scripts.launcher.load_config_value", return_value=8765):
            assert backend_port() == 8765

    def test_inference_port_returns_default(self):
        with patch("scripts.launcher.load_config_value", return_value=8766):
            assert inference_port() == 8766

    def test_load_config_value_with_valid_yaml(self, tmp_path):
        config_file = tmp_path / "test_config.yaml"
        config_file.write_text("dashboard:\n  port: 9000\n", encoding="utf-8")
        with patch("scripts.launcher.CONFIG_PATH", config_file):
            assert load_config_value("dashboard", "port", default=8765) == 9000


class TestGpuDetection:
    def test_detect_gpu_vendor_cpu_fallback(self):
        with patch("shutil.which", return_value=None):
            with patch("platform.system", return_value="Windows"):
                assert detect_gpu_vendor() == "cpu"

    def test_detect_gpu_vendor_nvidia(self):
        def mock_which(cmd):
            return "/usr/bin/nvidia-smi" if cmd == "nvidia-smi" else None

        with patch("shutil.which", side_effect=mock_which):
            assert detect_gpu_vendor() == "nvidia"

    def test_detect_gpu_vendor_amd(self):
        def mock_which(cmd):
            if cmd == "rocminfo":
                return "/opt/rocm/bin/rocminfo"
            return None

        with patch("shutil.which", side_effect=mock_which):
            assert detect_gpu_vendor() == "amd"


class TestVenvPython:
    def test_venv_python_returns_path(self):
        result = venv_python()
        assert isinstance(result, Path)
        assert ".venv" in str(result)

    def test_on_windows_returns_bool(self):
        assert isinstance(on_windows(), bool)
