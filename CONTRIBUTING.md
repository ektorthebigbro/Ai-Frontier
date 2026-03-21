# Contributing to AI Frontier

Thank you for your interest in contributing to AI Frontier! This guide will help you get started.

## Development Setup

### Prerequisites

- Python 3.10+
- CMake 3.16+
- Qt 6.5+ (for dashboard builds)
- A C++17 compiler (MSVC, GCC 9+, or Clang 10+)
- NVIDIA CUDA toolkit (optional, for GPU training) or AMD ROCm (optional, for AMD GPU training)

### Getting Started

```bash
# Clone the repository
git clone https://github.com/ektorthebigbro/ai-model-project.git
cd ai-model-project

# Set up the Python environment
python scripts/launcher.py setup

# Build the native backend
python scripts/launcher.py build backend

# Start the backend server
python scripts/launcher.py server

# Send actions to the running backend
python scripts/launcher.py action training
python scripts/launcher.py action evaluate
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `QT_DIR` or `QTDIR` | Path to Qt installation (for CMake) |
| `AI_FRONTIER_BACKEND_URL` | Backend URL override (default: `http://127.0.0.1:8765`) |
| `AI_FRONTIER_TORCH_INDEX_URL` | Custom PyTorch wheel index URL |
| `AI_FRONTIER_CMAKE_GENERATOR` | CMake generator override (e.g., `Ninja`) |
| `HF_TOKEN` | Hugging Face API token for gated models |
| `CUDA_VISIBLE_DEVICES` | GPU device selection for NVIDIA (default: `0`) |
| `HIP_VISIBLE_DEVICES` | GPU device selection for AMD ROCm (default: `0`) |

## Code Style

### Python
- 4-space indentation
- `snake_case` for functions and variables
- `PascalCase` for classes
- Use `pathlib.Path` for file paths
- Type hints on function signatures

### C++
- C++17 standard
- `PascalCase` for classes and types
- `camelCase` for local variables and methods
- `m_` prefix for member variables
- Use `QStringLiteral` for string constants

### QML
- 4-space indentation
- Properties at the top of components
- Signal handlers after properties

## Pull Request Process

1. Fork the repository and create a feature branch
2. Make your changes with clear, descriptive commits
3. Ensure all tests pass: `python -m pytest`
4. Ensure Python code passes linting: `ruff check .`
5. Submit a pull request with a clear description of what and why

## Project Structure

```
frontier/          # Core Python library (config, hardware, data, modeling)
training/          # Training loop and checkpointing
evaluation/        # Benchmark evaluation
inference/         # FastAPI inference server
dataset/           # Dataset preparation
critic/            # Critic model for RL training
rl/                # RL training loop
backend/           # C++ native backend (HTTP API server)
dashboard_widgets/ # Qt Widgets desktop dashboard (legacy)
dashboard_qml/     # Qt Quick/QML desktop dashboard (active)
scripts/           # Cross-platform launcher
configs/           # Runtime configuration (YAML)
tests/             # Python tests
```

## Reporting Issues

Please use [GitHub Issues](https://github.com/ektorthebigbro/ai-model-project/issues) to report bugs or request features. Include:
- Steps to reproduce
- Expected vs actual behavior
- OS, Python version, GPU info
