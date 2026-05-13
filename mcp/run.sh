#!/usr/bin/env bash
# Launch the MCP server using the local venv
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/.venv/bin/python3" "$SCRIPT_DIR/server.py" "$@"
