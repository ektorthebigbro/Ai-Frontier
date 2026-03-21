#!/usr/bin/env python3
"""Cross-platform launcher for AI Frontier workflows."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TMP_DIR = PROJECT_ROOT / ".tmp"
LOG_DIR = PROJECT_ROOT / "logs"
FEED_PATH = LOG_DIR / "dashboard_metrics.jsonl"
REQUIREMENTS_PATH = PROJECT_ROOT / "requirements.txt"
CONFIG_PATH = PROJECT_ROOT / "configs" / "default.yaml"


def load_config_value(*keys: str, default: object = None) -> object:
    """Read a nested value from configs/default.yaml without requiring PyYAML."""
    try:
        import yaml  # noqa: F811

        with CONFIG_PATH.open(encoding="utf-8") as f:
            cfg = yaml.safe_load(f) or {}
        for key in keys:
            if not isinstance(cfg, dict):
                return default
            cfg = cfg.get(key)
            if cfg is None:
                return default
        return cfg
    except Exception:
        return default


def backend_port() -> int:
    return int(load_config_value("dashboard", "port", default=8765))


def inference_port() -> int:
    return int(load_config_value("inference", "port", default=8766))


def ensure_dir(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    return path


def emit_progress(job: str, stage: str, progress: float, message: str) -> None:
    print(f"AI_PROGRESS|{job}|{stage}|{progress:.3f}|{message}", flush=True)
    ensure_dir(LOG_DIR)
    row = {
        "ts": int(time.time()),
        "job": job,
        "stage": stage,
        "message": message,
        "progress": float(progress),
    }
    with FEED_PATH.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row, separators=(",", ":")) + "\n")


def on_windows() -> bool:
    return os.name == "nt"


def default_python_command() -> list[str]:
    if shutil.which("python"):
        return ["python"]
    if on_windows() and shutil.which("py"):
        return ["py", "-3"]
    if shutil.which("python3"):
        return ["python3"]
    raise RuntimeError("Could not find a Python interpreter on PATH")


def venv_python() -> Path:
    if on_windows():
        return PROJECT_ROOT / ".venv" / "Scripts" / "python.exe"
    python3 = PROJECT_ROOT / ".venv" / "bin" / "python3"
    if python3.exists():
        return python3
    return PROJECT_ROOT / ".venv" / "bin" / "python"


def runtime_env(temp_subdir: str) -> dict[str, str]:
    env = os.environ.copy()
    temp_dir = ensure_dir(TMP_DIR / temp_subdir)
    existing_pythonpath = env.get("PYTHONPATH", "").strip()
    pythonpath_parts = [str(PROJECT_ROOT)]
    if existing_pythonpath:
        pythonpath_parts.append(existing_pythonpath)
    env["PYTHONPATH"] = os.pathsep.join(pythonpath_parts)
    env.setdefault("CUDA_VISIBLE_DEVICES", "0")
    env.setdefault("HIP_VISIBLE_DEVICES", "0")
    env["TEMP"] = str(temp_dir)
    env["TMP"] = str(temp_dir)
    env["TMPDIR"] = str(temp_dir)

    qt_dir = env.get("QT_DIR") or env.get("QTDIR")
    if qt_dir:
        qt_bin = Path(qt_dir) / "bin"
        if qt_bin.exists():
            existing_path = env.get("PATH", "")
            env["PATH"] = os.pathsep.join([str(qt_bin), existing_path]) if existing_path else str(qt_bin)
    return env


def run_command(
    command: Iterable[str | os.PathLike[str]],
    *,
    env: dict[str, str] | None = None,
    cwd: Path | None = None,
    check: bool = True,
) -> int:
    command_list = [str(part) for part in command]
    process = subprocess.run(command_list, cwd=cwd or PROJECT_ROOT, env=env, check=False)
    if check and process.returncode != 0:
        raise subprocess.CalledProcessError(process.returncode, command_list)
    return process.returncode


def requirements_without_torch(target_path: Path) -> Path:
    lines = []
    for raw_line in REQUIREMENTS_PATH.read_text(encoding="utf-8").splitlines():
        stripped = raw_line.strip()
        if stripped.lower().startswith("torch"):
            continue
        lines.append(raw_line)
    target_path.write_text("\n".join(lines).strip() + "\n", encoding="utf-8")
    return target_path


def detect_gpu_vendor() -> str:
    """Detect GPU vendor: 'nvidia', 'amd', or 'cpu'."""
    if shutil.which("nvidia-smi") is not None:
        return "nvidia"
    if shutil.which("rocminfo") is not None or shutil.which("rocm-smi") is not None:
        return "amd"
    # Check for AMD GPUs on Linux via /sys
    if platform.system() == "Linux":
        try:
            lspci_output = subprocess.run(
                ["lspci"], capture_output=True, text=True, check=False
            ).stdout.lower()
            if "amd" in lspci_output and ("vga" in lspci_output or "display" in lspci_output):
                return "amd"
        except FileNotFoundError:
            pass
    return "cpu"


def install_torch(python_exe: Path, env: dict[str, str]) -> None:
    system = platform.system()
    gpu_vendor = detect_gpu_vendor()
    torch_index_url = env.get("AI_FRONTIER_TORCH_INDEX_URL", "").strip()

    command = [str(python_exe), "-m", "pip", "install", "--upgrade", "--force-reinstall", "--no-cache-dir"]
    if torch_index_url:
        command.extend(["--index-url", torch_index_url])
    elif system in {"Windows", "Linux"} and gpu_vendor == "nvidia":
        command.extend(["--index-url", "https://download.pytorch.org/whl/cu128"])
    elif system == "Linux" and gpu_vendor == "amd":
        command.extend(["--index-url", "https://download.pytorch.org/whl/rocm6.3"])
    command.append("torch")
    print(f"Installing PyTorch (detected GPU: {gpu_vendor})")
    run_command(command, env=env)


def verify_torch_runtime(python_exe: Path, env: dict[str, str]) -> None:
    check_script = """
import platform
import torch
device = "CPU"
if torch.cuda.is_available():
    device = torch.cuda.get_device_name(0)
elif hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
    device = "Apple Metal (MPS)"
print("Torch:", torch.__version__)
print("Platform:", platform.platform())
print("Torch CUDA build:", torch.version.cuda or "not bundled")
print("CUDA available:", torch.cuda.is_available())
print("MPS available:", bool(hasattr(torch.backends, "mps") and torch.backends.mps.is_available()))
print("Device:", device)
"""
    run_command([str(python_exe), "-c", check_script], env=env)


def setup_environment(_: argparse.Namespace) -> int:
    ensure_dir(TMP_DIR)
    ensure_dir(LOG_DIR)

    emit_progress("setup", "create_venv", 0.10, "Creating virtual environment")
    if not (PROJECT_ROOT / ".venv").exists():
        run_command([*default_python_command(), "-m", "venv", str(PROJECT_ROOT / ".venv")], env=runtime_env("setup"))

    python_exe = venv_python()
    if not python_exe.exists():
        raise RuntimeError(f"Virtual environment Python was not created: {python_exe}")

    env = runtime_env("setup")

    emit_progress("setup", "upgrade_pip", 0.28, "Upgrading pip")
    run_command([str(python_exe), "-m", "pip", "install", "--upgrade", "pip"], env=env)

    emit_progress("setup", "repair_packages", 0.42, "Clearing stale torch packages")
    run_command([str(python_exe), "-m", "pip", "uninstall", "-y", "torch", "torchvision", "torchaudio"], env=env, check=False)

    emit_progress("setup", "install_dependencies", 0.60, "Installing non-Torch dependencies")
    requirements_path = requirements_without_torch(ensure_dir(TMP_DIR) / "requirements.no_torch.txt")
    run_command([str(python_exe), "-m", "pip", "install", "-r", str(requirements_path)], env=env)

    emit_progress("setup", "install_torch", 0.82, "Installing PyTorch")
    install_torch(python_exe, env)

    emit_progress("setup", "verify_runtime", 0.94, "Verifying runtime")
    verify_torch_runtime(python_exe, env)

    emit_progress("setup", "completed", 1.0, "Environment setup complete")
    print("Environment setup complete.")
    print("Next steps:")
    print("  python scripts/launcher.py server")
    print("  python scripts/launcher.py training --resume")
    print("  python scripts/launcher.py build widgets")
    return 0


def run_python_module(script_relative_path: str, extra_args: list[str], temp_subdir: str, banner: str) -> int:
    python_exe = venv_python()
    if not python_exe.exists():
        raise RuntimeError("Virtual environment is missing. Run setup first.")

    env = runtime_env(temp_subdir)
    print(banner)
    return run_command([str(python_exe), str(PROJECT_ROOT / script_relative_path), *extra_args], env=env)


def pick_generator() -> list[str]:
    generator = os.environ.get("AI_FRONTIER_CMAKE_GENERATOR", "").strip() or os.environ.get("CMAKE_GENERATOR", "").strip()
    if generator:
        return ["-G", generator]
    if shutil.which("ninja"):
        return ["-G", "Ninja"]
    return []


def cmake_prefix_args() -> list[str]:
    qt_dir = os.environ.get("QT_DIR", "").strip() or os.environ.get("QTDIR", "").strip()
    if qt_dir:
        return [f"-DCMAKE_PREFIX_PATH={qt_dir}"]
    return []


def build_target(project_dir: Path, target: str) -> Path:
    build_dir = project_dir / "build"
    env = runtime_env("build")
    env.setdefault("QTFRAMEWORK_BYPASS_LICENSE_CHECK", "1")

    configure_command = [
        "cmake",
        "-S",
        str(project_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        *pick_generator(),
        *cmake_prefix_args(),
    ]
    print(f"Configuring {target} in {build_dir} ...")
    run_command(configure_command, env=env)

    build_command = [
        "cmake",
        "--build",
        str(build_dir),
        "--config",
        "Release",
        "--target",
        target,
        "--parallel",
    ]
    print(f"Building {target} ...")
    run_command(build_command, env=env)
    binary = locate_built_binary(build_dir, target)
    print(f"Built {target}: {binary}")
    return binary


def locate_built_binary(build_dir: Path, target: str) -> Path:
    if on_windows():
        candidates = [
            build_dir / f"{target}.exe",
            build_dir / "Release" / f"{target}.exe",
        ]
    else:
        candidates = [
            build_dir / target,
            build_dir / "Release" / target,
        ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    bundle_path = build_dir / f"{target}.app" / "Contents" / "MacOS" / target
    if bundle_path.exists():
        return bundle_path

    bundle_matches = sorted(build_dir.rglob(f"{target}.app"))
    for bundle in bundle_matches:
        bundle_exec = bundle / "Contents" / "MacOS" / target
        if bundle_exec.exists():
            return bundle_exec

    patterns = [f"{target}.exe", target]
    for pattern in patterns:
        matches = sorted(build_dir.rglob(pattern))
        for match in matches:
            if match.is_file():
                return match

    raise FileNotFoundError(f"Could not find built target '{target}' under {build_dir}")


def existing_built_binary(build_dir: Path, target: str) -> Path | None:
    try:
        return locate_built_binary(build_dir, target)
    except FileNotFoundError:
        return None


def run_native_binary(binary_path: Path, temp_subdir: str) -> int:
    env = runtime_env(temp_subdir)
    return run_command([str(binary_path)], env=env, cwd=binary_path.parent)


def handle_build(args: argparse.Namespace) -> int:
    target_map = {
        "backend": (PROJECT_ROOT / "dashboard", "ai_frontier_backend_api"),
        "widgets": (PROJECT_ROOT / "dashboard_qt_cpp", "ai_frontier_qt_dashboard"),
        "qml": (PROJECT_ROOT / "dashboard_qt_cpp_v2", "ai_frontier_qt_dashboard"),
    }
    project_dir, target = target_map[args.target]
    build_target(project_dir, target)
    return 0


def handle_run(args: argparse.Namespace) -> int:
    target_map = {
        "widgets": (PROJECT_ROOT / "dashboard_qt_cpp", "ai_frontier_qt_dashboard", "dashboard_widgets"),
        "qml": (PROJECT_ROOT / "dashboard_qt_cpp_v2", "ai_frontier_qt_dashboard", "dashboard_qml"),
    }
    project_dir, target, temp_subdir = target_map[args.target]
    binary = existing_built_binary(project_dir / "build", target)
    if binary is None or not args.skip_build:
        binary = build_target(project_dir, target)
    return run_native_binary(binary, temp_subdir)


def handle_server(args: argparse.Namespace) -> int:
    binary = existing_built_binary(PROJECT_ROOT / "dashboard" / "build", "ai_frontier_backend_api")
    if binary is None or not args.skip_build:
        binary = build_target(PROJECT_ROOT / "dashboard", "ai_frontier_backend_api")
    print(f"Starting native backend console on http://127.0.0.1:{backend_port()}")
    return run_native_binary(binary, "server")


def handle_action(args: argparse.Namespace) -> int:
    """Send an action to the running backend via HTTP POST."""
    import urllib.request
    import urllib.error

    port = backend_port()
    action_name = args.name
    url = f"http://127.0.0.1:{port}/api/actions/{action_name}/start"
    print(f"Sending action '{action_name}' to backend at http://127.0.0.1:{port}")

    try:
        request = urllib.request.Request(url, method="POST", data=b"{}")
        request.add_header("Content-Type", "application/json")
        with urllib.request.urlopen(request, timeout=10) as response:
            body = json.loads(response.read().decode("utf-8"))
            ok = body.get("ok", True)
            message = body.get("message", "")
            if ok:
                print(f"Action '{action_name}' started: {message}" if message else f"Action '{action_name}' started")
            else:
                print(f"Action '{action_name}' failed: {message}")
            return 0 if ok else 1
    except urllib.error.URLError as exc:
        print(f"Could not reach backend at http://127.0.0.1:{port}. Is the backend running?")
        print(f"  Error: {exc}")
        print(f"  Start the backend first: python scripts/launcher.py server")
        return 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Cross-platform AI Frontier launcher")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("setup", help="Create the virtual environment and install dependencies")

    build = subparsers.add_parser("build", help="Build a native target")
    build.add_argument("target", choices=["backend", "widgets", "qml"])

    run = subparsers.add_parser("run", help="Run a native dashboard")
    run.add_argument("target", choices=["widgets", "qml"])
    run.add_argument("--skip-build", action="store_true", help="Skip the build step and launch the existing binary")

    server = subparsers.add_parser("server", help="Build and run the native backend")
    server.add_argument("--skip-build", action="store_true", help="Skip the build step and launch the existing backend")

    action = subparsers.add_parser("action", help="Send an action to the running backend (e.g. training, evaluate, prepare, inference)")
    action.add_argument("name", help="Action name (training, evaluate, prepare, inference, autopilot, setup)")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "setup":
        return setup_environment(args)
    if args.command == "build":
        return handle_build(args)
    if args.command == "run":
        return handle_run(args)
    if args.command == "server":
        return handle_server(args)
    if args.command == "action":
        return handle_action(args)

    parser.error(f"Unsupported command: {args.command}")
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        raise SystemExit(exc.returncode) from exc
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
