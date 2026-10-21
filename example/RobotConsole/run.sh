#!/usr/bin/env bash
# SurgeonConsole — Linux / 树莓派启动（与可执行文件同目录）。
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || realpath "$0" 2>/dev/null || echo "$0")")" && pwd)"
cd "$HERE"

export DISPLAY="${DISPLAY:-:0}"
export TCGUICORE_FULLSCREEN="${TCGUICORE_FULLSCREEN:-1}"
export TCGUICORE_MAXIMIZE="${TCGUICORE_MAXIMIZE:-1}"

# 启动前检测 USB 触摸屏；缺失则刷新驱动（需要 NOPASSWD sudo）。
TOUCH_ENSURE=""
for c in "$HERE/ensure_touchscreen.sh" \
         "$HERE/../../scripts/ensure_touchscreen.sh" \
         /usr/local/sbin/surgeon-console-ensure-touch.sh; do
  if [[ -x "$c" ]]; then
    TOUCH_ENSURE="$c"
    break
  fi
done
if [[ -n "$TOUCH_ENSURE" ]]; then
  sudo -n "$TOUCH_ENSURE" || "$TOUCH_ENSURE" || true
fi

# Prefer release binary next to this script, else repo build output.
if [[ -x "$HERE/SurgeonConsole" ]]; then
  exec "$HERE/SurgeonConsole" "$@"
fi
exec "$HERE/../../bin/lin-arm64/release/SurgeonConsole" "$@"
