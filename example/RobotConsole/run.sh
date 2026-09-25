#!/usr/bin/env bash
# SurgeonConsole — Linux / 树莓派启动（与可执行文件同目录）。
set -euo pipefail
cd "$(dirname "$(readlink -f "$0" 2>/dev/null || realpath "$0" 2>/dev/null || echo "$0")")"

export DISPLAY="${DISPLAY:-:0}"
# 触摸屏全屏：export TCGUICORE_MAXIMIZE=1

exec ./SurgeonConsole "$@"
