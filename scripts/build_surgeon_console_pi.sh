#!/usr/bin/env bash
# 在树莓派 / Linux 本机编译 SurgeonConsole（与 Windows 同一工程、同一产物名）。
# 用法：
#   cd /path/to/tcGUICore
#   chmod +x scripts/build_surgeon_console_pi.sh
#   ./scripts/build_surgeon_console_pi.sh
#
# 产物：bin/lin-arm64/release/SurgeonConsole（及旁路 SurgeonConsole.json）
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-$ROOT/build-pi}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "==> 检查依赖"
need_pkgs=()
for pkg in build-essential cmake pkg-config \
    libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev; do
  if ! dpkg -s "$pkg" >/dev/null 2>&1; then
    need_pkgs+=("$pkg")
  fi
done
if ((${#need_pkgs[@]})); then
  echo "缺少: ${need_pkgs[*]}"
  echo "执行: sudo apt update && sudo apt install -y ${need_pkgs[*]} fonts-noto-cjk"
  exit 1
fi

echo "==> CMake 配置（关闭 NDI）"
cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DTCGUICORE_ENABLE_NDI=OFF \
  -DTCGUICORE_LINK_NDI=OFF \
  -DTCGUICORE_BUILD_EXAMPLES=ON

echo "==> 编译 RobotConsole → SurgeonConsole"
cmake --build "$BUILD_DIR" --target RobotConsole -j"$JOBS"

arch="$(uname -m)"
case "$arch" in
  aarch64|arm64) PLATFORM="lin-arm64" ;;
  armv7l|armv6l) PLATFORM="lin-arm" ;;
  *) PLATFORM="lin-x64" ;;
esac

OUT="$ROOT/bin/${PLATFORM}/release"
echo
echo "完成。可执行文件："
echo "  $OUT/SurgeonConsole"
echo "  $OUT/SurgeonConsole.json"
echo
echo "运行："
echo "  cd \"$OUT\" && ./SurgeonConsole"
echo "  # 或: TCGUICORE_MAXIMIZE=1 ./SurgeonConsole"
ls -lah "$OUT"/SurgeonConsole* 2>/dev/null || ls -lah "$OUT"
