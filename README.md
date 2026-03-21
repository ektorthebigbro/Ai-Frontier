# AI Frontier

Local-first, cross-platform research ecosystem for training, evaluating, and serving transformer models.

## Highlights

- End-to-end local workflow: dataset prep, training, evaluation, and inference
- Native C++ backend with HTTP API for process management, metrics, and diagnostics
- Qt Quick/QML desktop dashboard with glassmorphism UI
- Cross-platform: single `python scripts/launcher.py` entry point (Windows, macOS, Linux)
- NVIDIA CUDA, AMD ROCm, and Apple MPS GPU support with CPU fallback
- Small custom transformer stack tuned for constrained hardware (tested on RTX 3060 6GB)

## Project Layout

```
frontier/            Core Python library (config, hardware, data, modeling, optimization)
training/            Training loop and checkpointing
evaluation/          Benchmark evaluation and report generation
inference/           FastAPI inference server
dataset_pipeline/    Dataset preparation and tokenizer workflow
dashboard/           C++ native backend (HTTP API server)
dashboard_qt_cpp/    Qt Widgets desktop dashboard (legacy)
dashboard_qt_cpp_v2/ Qt Quick/QML desktop dashboard (active)
scripts/             Cross-platform launcher
configs/             Runtime configuration (YAML)
tests/               Python tests
```

## Requirements

- Python 3.10+
- CMake 3.16+
- Qt 6.5+ (for dashboard builds)
- C++17 compiler (MSVC, GCC 9+, or Clang 10+)

GPU support (optional):
- NVIDIA: CUDA toolkit + `nvidia-smi` on PATH
- AMD: ROCm + `rocminfo` on PATH
- Apple Silicon: MPS support built into PyTorch

## Quick Start

```bash
# 1. Set up the Python environment (creates .venv, installs PyTorch + deps)
python scripts/launcher.py setup

# 2. Build and start the native backend
python scripts/launcher.py server

# 3. Send actions to the backend (from another terminal)
python scripts/launcher.py action training
python scripts/launcher.py action evaluate
python scripts/launcher.py action inference
python scripts/launcher.py action prepare
```

## All Commands

| Command | Description |
|---------|-------------|
| `setup` | Create virtual environment and install all dependencies |
| `server` | Build and run the native C++ backend |
| `server --skip-build` | Run the backend without rebuilding |
| `build backend` | Build only the backend binary |
| `build qml` | Build the Qt Quick/QML dashboard |
| `build widgets` | Build the Qt Widgets dashboard (legacy) |
| `run qml` | Build and run the QML dashboard |
| `run widgets` | Build and run the Widgets dashboard |
| `action <name>` | Send an action to the running backend |

Actions: `training`, `evaluate`, `prepare`, `inference`, `autopilot`, `setup`

## Configuration

Runtime config lives in `configs/default.yaml`. Key settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `dashboard.port` | 8765 | Backend API port |
| `inference.port` | 8766 | Inference server port |
| `training.batch_size` | 8 | Training batch size |
| `training.learning_rate` | 0.0002 | Learning rate |
| `training.vram_ceiling_gb` | 5.5 | VRAM usage cap |
| `model.vocab_size` | 32000 | Tokenizer vocabulary size |
| `model.max_seq_len` | 2048 | Maximum sequence length |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `QT_DIR` / `QTDIR` | Qt installation path (forwarded to CMake) |
| `AI_FRONTIER_BACKEND_URL` | Override backend URL for dashboards |
| `AI_FRONTIER_TORCH_INDEX_URL` | Custom PyTorch wheel index |
| `AI_FRONTIER_CMAKE_GENERATOR` | CMake generator override |
| `HF_TOKEN` | Hugging Face token for gated models |
| `CUDA_VISIBLE_DEVICES` | NVIDIA GPU selection (default: `0`) |
| `HIP_VISIBLE_DEVICES` | AMD GPU selection (default: `0`) |

## Architecture

```
                     configs/default.yaml
                            |
                    +-------+-------+
                    |               |
              C++ Backend      Python Workers
           (dashboard/)     (training, eval, etc.)
                |                   |
          HTTP API :8765      QProcess management
                |
        +-------+-------+
        |               |
   QML Dashboard    CLI actions
  (dashboard_qt_cpp_v2)  (launcher.py action)
```

The C++ backend is the central orchestrator. It manages Python workers as subprocesses, tracks metrics, handles diagnostics, and exposes a REST API. Dashboards and CLI commands communicate with it via HTTP.

## Building with CMake

The root `CMakeLists.txt` supports building all targets:

```bash
cmake -S . -B build -DBUILD_BACKEND=ON -DBUILD_DASHBOARD_QML=ON
cmake --build build
```

Options: `BUILD_BACKEND` (ON), `BUILD_DASHBOARD_WIDGETS` (OFF), `BUILD_DASHBOARD_QML` (OFF)

## Testing

```bash
python -m pytest
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, code style, and PR guidelines.

## License

[MIT](LICENSE)
