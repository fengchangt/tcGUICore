#!/usr/bin/env bash
# Ensure TSTP / Winbond USB MTouch (0416:c168|c169) is present with an input node.
# Boot often enumerates 0416:c168 first (HID parse fails), then disconnects; sometimes
# it never comes back as 0416:c169. Reset USB until a working input device appears.
#
# Usage:
#   ensure_touchscreen.sh            # detect; reset only if needed
#   ensure_touchscreen.sh --force    # always USB-reset once, then wait
#   ensure_touchscreen.sh --status   # print status and exit 0/1
set -u

LOG_DIR="${XDG_STATE_HOME:-${HOME:-/tmp}/.local/state}/surgeon-console"
mkdir -p "$LOG_DIR" 2>/dev/null || true
LOG="$LOG_DIR/touchscreen.log"

log() {
  local line="$(date -Iseconds) $*"
  printf '%s\n' "$line" >>"$LOG" 2>/dev/null || true
  printf '%s\n' "$*"
}

vendor_ok() {
  # 0416 = Winbond / TSTP MTouch
  grep -qiE '0416:c168|0416:c169' <<<"$(lsusb 2>/dev/null)"
}

bad_only() {
  # Stuck in the failed HID firmware/enum mode.
  local out
  out="$(lsusb 2>/dev/null || true)"
  grep -qi '0416:c168' <<<"$out" && ! grep -qi '0416:c169' <<<"$out"
}

input_ok() {
  # Working touch reports as absolute mouse named TSTP MTouch.
  if [[ -e /dev/input/by-id/usb-TSTP_MTouch_CMTP_1.0-event-mouse ]]; then
    return 0
  fi
  if [[ -e /dev/input/by-id/usb-TSTP_MTouch_CMTP_1.0-event-if00 ]]; then
    return 0
  fi
  if grep -q 'Name="TSTP MTouch"' /proc/bus/input/devices 2>/dev/null; then
    return 0
  fi
  # Fallback: any 0416 input node with ABS axes.
  local name=""
  while IFS= read -r line; do
    case "$line" in
      N:\ Name=*) name="${line#N: Name=}" ;;
      I:\ Bus=*Product=c169*|I:\ Bus=*Product=c168*)
        :
        ;;
      B:\ ABS=*)
        if [[ -n "$name" && "$name" == *MTouch* && "$line" != *ABS=0* ]]; then
          return 0
        fi
        ;;
      "")
        name=""
        ;;
    esac
  done < /proc/bus/input/devices
  return 1
}

touch_ok() {
  input_ok && vendor_ok && ! bad_only
}

sysfs_mtouch_dirs() {
  local d v p
  for d in /sys/bus/usb/devices/*; do
    [[ -f "$d/idVendor" && -f "$d/idProduct" ]] || continue
    v=$(cat "$d/idVendor" 2>/dev/null || true)
    p=$(cat "$d/idProduct" 2>/dev/null || true)
    if [[ "$v" == "0416" && ( "$p" == "c168" || "$p" == "c169" ) ]]; then
      printf '%s\n' "$d"
    fi
  done
}

run_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
    return $?
  fi
  if command -v sudo >/dev/null 2>&1; then
    sudo -n "$@" 2>/dev/null
    return $?
  fi
  return 1
}

xhci_rebind() {
  # When the device node is gone (authorized=0 / bad enum), only rebinding the
  # host controller brings TSTP MTouch back. Prefer xhci-hcd.1 (Pi5 RP1 bus3).
  local drv="/sys/bus/platform/drivers/xhci-hcd"
  [[ -d "$drv" ]] || return 1
  local id
  for id in xhci-hcd.1 xhci-hcd.0; do
    if [[ -e "$drv/$id" || -e "/sys/devices/platform/axi/"*"/$id" || -e /sys/bus/platform/devices/$id ]]; then
      log "rebind USB host $id"
      run_root sh -c "echo '$id' > '$drv/unbind'" || true
      sleep 1.5
      run_root sh -c "echo '$id' > '$drv/bind'" || true
      sleep 2
      return 0
    fi
  done
  # Fallback: rebind every bound xhci-hcd.* under the platform driver.
  for id in "$drv"/xhci-hcd.*; do
    [[ -e "$id" ]] || continue
    id=$(basename "$id")
    log "rebind USB host $id"
    run_root sh -c "echo '$id' > '$drv/unbind'" || true
    sleep 1.5
    run_root sh -c "echo '$id' > '$drv/bind'" || true
    sleep 2
  done
}

usb_reset_mtouch() {
  log "resetting TSTP MTouch USB…"
  local present=0
  if vendor_ok; then
    present=1
  fi

  if [[ "$present" -eq 1 ]]; then
    # Soft path while the device is still on the bus — do NOT write authorized=0
    # (on this Pi it removes the sysfs node and the device stays gone).
    if command -v usbreset >/dev/null 2>&1; then
      run_root usbreset 0416:c168 >/dev/null 2>&1 || true
      run_root usbreset 0416:c169 >/dev/null 2>&1 || true
    fi

    run_root python3 - <<'PY' >/dev/null 2>&1 || true
import fcntl, os, subprocess
USBDEVFS_RESET = 21780
try:
    out = subprocess.check_output(["lsusb"], text=True)
except Exception:
    raise SystemExit(0)
for line in out.splitlines():
    if "0416:c168" not in line and "0416:c169" not in line:
        continue
    parts = line.split()
    bus = int(parts[1])
    dev = int(parts[3].rstrip(":"))
    path = f"/dev/bus/usb/{bus:03d}/{dev:03d}"
    try:
        fd = os.open(path, os.O_WRONLY)
    except OSError:
        continue
    try:
        fcntl.ioctl(fd, USBDEVFS_RESET, 0)
    except OSError:
        pass
    finally:
        os.close(fd)
PY

    for iface in /sys/bus/usb/drivers/usbhid/*; do
      base=$(basename "$iface")
      [[ "$base" == *:* ]] || continue
      real=$(readlink -f "$iface" 2>/dev/null || true)
      if [[ -n "$real" ]] && grep -qE 'c168|c169' "$real/../idProduct" 2>/dev/null; then
        run_root sh -c "echo '$base' > /sys/bus/usb/drivers/usbhid/unbind; sleep 0.2; echo '$base' > /sys/bus/usb/drivers/usbhid/bind" || true
      fi
    done
    sleep 0.5
    if touch_ok; then
      return 0
    fi
  fi

  # Hard path: device missing or stuck in 0416:c168 — rebind host controller.
  xhci_rebind || true
}

wait_for_touch() {
  local i
  for i in $(seq 1 24); do
    if touch_ok; then
      return 0
    fi
    sleep 0.25
  done
  return 1
}

status_line() {
  local usb input
  usb="$(lsusb 2>/dev/null | grep -E '0416:c168|0416:c169' || true)"
  if input_ok; then
    input="input=ok"
  else
    input="input=missing"
  fi
  if [[ -z "$usb" ]]; then
    printf 'usb=missing %s\n' "$input"
  else
    printf 'usb=[%s] %s\n' "$usb" "$input"
  fi
}

FORCE=0
STATUS_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --force) FORCE=1 ;;
    --status) STATUS_ONLY=1 ;;
    -h|--help)
      sed -n '2,12p' "$0"
      exit 0
      ;;
  esac
done

if [[ "$STATUS_ONLY" -eq 1 ]]; then
  status_line
  touch_ok && exit 0 || exit 1
fi

if [[ "$FORCE" -eq 0 ]] && touch_ok; then
  log "touchscreen OK ($(status_line | tr '\n' ' '))"
  exit 0
fi

log "touchscreen not ready ($(status_line | tr '\n' ' ')); attempting USB refresh"
attempts=3
[[ "$FORCE" -eq 1 ]] && attempts=2
for a in $(seq 1 "$attempts"); do
  usb_reset_mtouch
  if wait_for_touch; then
    log "touchscreen recovered after attempt $a ($(status_line | tr '\n' ' '))"
    exit 0
  fi
  log "attempt $a failed ($(status_line | tr '\n' ' '))"
done

log "touchscreen still missing after refresh ($(status_line | tr '\n' ' '))"
exit 1
