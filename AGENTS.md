# Repository Guidelines

## Project Structure & Module Organization
`frontier/` contains shared Python core logic for config, hardware, data, modeling, optimization, and model management. Main workflows live in `training/`, `dataset/`, `evaluation/`, `inference/`, `critic/`, and `rl/`. The C++ native backend is in `backend/` with its own `CMakeLists.txt`. Desktop clients are in `dashboard_widgets/` (legacy Qt Widgets) and `dashboard_qml/` (active Qt Quick/QML). Runtime config lives in `configs/default.yaml`. Generated outputs belong in `artifacts/`, `checkpoints/`, `logs/`, and `data/cache` or `data/processed`; keep large generated files out of Git.

## Build, Test, and Development Commands
- `python scripts/launcher.py setup` - creates `.venv`, installs dependencies, auto-detects GPU vendor (NVIDIA/AMD/CPU), and installs the correct PyTorch wheels.
- `python scripts/launcher.py server` - builds and starts the native C++ backend.
- `python scripts/launcher.py action <name>` - sends an action to the running backend (training, evaluate, prepare, inference, autopilot).
- `python scripts/launcher.py build backend|widgets|qml` - builds a native CMake target.
- `python scripts/launcher.py run widgets|qml` - builds and runs a dashboard.
- `python -m pytest` - runs the Python test suite.
- `ruff check .` - lints Python code.

All operations go through `scripts/launcher.py` (cross-platform). There are no platform-specific `.bat` or `.sh` scripts.

## Architecture
The C++ backend (`backend/`) is the central orchestrator. It manages Python workers via QProcess, exposes a REST API on the port configured in `configs/default.yaml` (default 8765), and handles metrics, diagnostics, and state. Dashboards connect to it via HTTP. The `scripts/launcher.py action` command also communicates via HTTP.

## Coding Style & Naming Conventions
- **Python**: 4-space indent, `snake_case` functions, `PascalCase` classes, `pathlib.Path` for paths, type hints on signatures.
- **C++**: C++17, `PascalCase` classes, `camelCase` locals, `m_` prefix for members, `QStringLiteral` for string constants.
- **QML**: 4-space indent, properties at top, signal handlers after properties.

## Testing Guidelines
Run `python -m pytest` for the full test suite. Tests live in `tests/` using `test_<module>.py` naming. For C++ changes, verify the launcher command starts cleanly. CI runs lint (ruff) + tests on all platforms.

## Commit & Pull Request Guidelines
Keep commit subjects short and imperative. Pull requests should summarize the affected area, list validation steps, and include screenshots for UI changes.

## Configuration
All ports, paths, and runtime values come from `configs/default.yaml`. Nothing is hardcoded. Key env vars: `QT_DIR`, `AI_FRONTIER_BACKEND_URL`, `HF_TOKEN`, `CUDA_VISIBLE_DEVICES`, `HIP_VISIBLE_DEVICES`.

## Security & Configuration Tips
Do not commit `.env`, `credentials.json`, model caches, checkpoints, or TensorBoard outputs. The backend redacts sensitive config keys (tokens, passwords, API keys) in API responses.
