#!/usr/bin/env bash
# Simple launcher to match autograder expectations
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PY_SRC="$SCRIPT_DIR/src"
PYTHONUNBUFFERED=1 python "$PY_SRC/main.py" "$@"

