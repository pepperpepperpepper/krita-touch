#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: touch-infra/collect-validation-device-info.sh [--linux-x11] [--output <path>]

Collects basic host/display/input info that is useful for documenting the
primary manual-validation device in plan.md.

Notes:
  - This is best-effort and will skip commands that are unavailable.
  - Some sections (like libinput) may require sudo on some distros.

Examples:
  touch-infra/collect-validation-device-info.sh --linux-x11
  touch-infra/collect-validation-device-info.sh --linux-x11 --output /tmp/device.md
EOF
}

MODE_LINUX_X11=0
OUTPUT_PATH=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --linux-x11)
      MODE_LINUX_X11=1
      ;;
    --output)
      shift
      OUTPUT_PATH="${1:-}"
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [[ ${MODE_LINUX_X11} -eq 0 ]]; then
  MODE_LINUX_X11=1
fi

if [[ -n "${OUTPUT_PATH}" ]]; then
  exec >"${OUTPUT_PATH}"
fi

have() { command -v "$1" >/dev/null 2>&1; }

UDEV_TOUCH_EVENT_HANDLERS=()

print_cmd_block() {
  local cmd_label="$1"
  shift

  echo
  echo "#### \`${cmd_label}\`"
  echo '```text'
  if "$@"; then
    :
  else
    echo "(command failed)"
  fi
  echo '```'
}

dump_drm_connectors() {
  if [[ ! -d /sys/class/drm ]]; then
    echo "(skipped: /sys/class/drm not present)"
    return 0
  fi

  shopt -s nullglob
  local any=0
  for dir in /sys/class/drm/card*-*; do
    [[ -f "${dir}/status" ]] || continue
    local status
    status="$(cat "${dir}/status" 2>/dev/null || true)"
    [[ "${status}" == "connected" ]] || continue
    any=1

    echo "$(basename "${dir}"): ${status}"
    if [[ -f "${dir}/mode" ]]; then
      echo "  current_mode: $(cat "${dir}/mode" 2>/dev/null || true)"
    fi
    if [[ -f "${dir}/modes" ]]; then
      echo "  modes (first 10):"
      sed -n '1,10p' "${dir}/modes" | sed 's/^/    /'
    fi
    if [[ -f "${dir}/edid" ]]; then
      local sz
      sz="$(wc -c <"${dir}/edid" 2>/dev/null || true)"
      echo "  edid_bytes: ${sz}"
    fi
    echo
  done
  shopt -u nullglob

  if [[ ${any} -eq 0 ]]; then
    echo "(no connected DRM connectors detected under /sys/class/drm)"
  fi
}

dump_drm_edid_decode() {
  if [[ ! -d /sys/class/drm ]]; then
    echo "(skipped: /sys/class/drm not present)"
    return 0
  fi

  if ! have edid-decode; then
    echo "(skipped: edid-decode not found; install it for panel model details)"
    return 0
  fi

  shopt -s nullglob
  local any=0
  for dir in /sys/class/drm/card*-*; do
    [[ -f "${dir}/status" ]] || continue
    local status
    status="$(cat "${dir}/status" 2>/dev/null || true)"
    [[ "${status}" == "connected" ]] || continue
    [[ -f "${dir}/edid" ]] || continue
    any=1

    echo "=== $(basename "${dir}") ==="
    edid-decode "${dir}/edid" 2>/dev/null || echo "(edid-decode failed)"
    echo
  done
  shopt -u nullglob

  if [[ ${any} -eq 0 ]]; then
    echo "(no EDID blobs found under connected DRM connectors)"
  fi
}

dump_input_symlinks() {
  if [[ -d /dev/input/by-id ]]; then
    echo "/dev/input/by-id:"
    ls -l /dev/input/by-id
  else
    echo "(no /dev/input/by-id)"
  fi

  echo

  if [[ -d /dev/input/by-path ]]; then
    echo "/dev/input/by-path:"
    ls -l /dev/input/by-path
  else
    echo "(no /dev/input/by-path)"
  fi
}

dump_udev_touch_devices() {
  if ! have udevadm; then
    echo "(skipped: udevadm not found)"
    return 0
  fi

  shopt -s nullglob
  local devs=(/dev/input/event*)
  shopt -u nullglob

  if [[ ${#devs[@]} -eq 0 ]]; then
    echo "(no /dev/input/event* devices)"
    return 0
  fi

  local any=0
  local dev
  for dev in "${devs[@]}"; do
    local props
    props="$(udevadm info -q property -n "${dev}" 2>/dev/null || true)"
    [[ -n "${props}" ]] || continue

    if echo "${props}" | grep -q '^ID_INPUT_TOUCHSCREEN=1$' || echo "${props}" | grep -q '^ID_INPUT_TABLET=1$'; then
      any=1
      UDEV_TOUCH_EVENT_HANDLERS+=("$(basename "${dev}")")
      echo "${dev}"
      echo "${props}" \
        | grep -E '^(NAME|ID_INPUT_TOUCHSCREEN|ID_INPUT_TABLET|ID_VENDOR_ID|ID_MODEL_ID|ID_MODEL|ID_PATH|ID_SERIAL|DEVPATH)=' \
        | sed 's/^/  /'
      echo
    fi
  done

  if [[ ${any} -eq 0 ]]; then
    echo "(no touchscreen/tablet devices detected via udev properties)"
  fi
}

dump_proc_bus_input_devices() {
  if [[ ! -r /proc/bus/input/devices ]]; then
    echo "(skipped: /proc/bus/input/devices not readable)"
    return 0
  fi

  if [[ ${#UDEV_TOUCH_EVENT_HANDLERS[@]} -eq 0 ]]; then
    cat /proc/bus/input/devices
    return 0
  fi

  if ! have awk; then
    echo "(note: awk not found; printing full /proc/bus/input/devices)"
    cat /proc/bus/input/devices
    return 0
  fi

  echo "Detected touch/tablet event nodes: ${UDEV_TOUCH_EVENT_HANDLERS[*]}"
  echo

  local re
  re="$(printf '%s|' "${UDEV_TOUCH_EVENT_HANDLERS[@]}")"
  re="${re%|}"

  if ! awk -v re="(${re})" 'BEGIN { RS=""; ORS="\n\n"; found=0 } $0 ~ re { print; found=1 } END { exit(found ? 0 : 42) }' /proc/bus/input/devices; then
    echo "(no matching blocks found; printing full /proc/bus/input/devices)"
    echo
    cat /proc/bus/input/devices
  fi
}

echo "### Validation device info (generated)"
echo
echo "- Collected at: $(date -Iseconds)"
echo "- Host: $(hostname 2>/dev/null || echo unknown)"
echo "- User: $(id -un 2>/dev/null || echo unknown)"
echo "- Kernel: $(uname -srmo 2>/dev/null || uname -a)"
echo "- Session: \`XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-unknown}\`"
echo "- Desktop: \`XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-unknown}\`"
echo "- Display: \`DISPLAY=${DISPLAY:-<unset>}\`"

if [[ -r /etc/os-release ]]; then
  os_pretty="$(. /etc/os-release && echo "${PRETTY_NAME:-}")" || os_pretty=""
  if [[ -n "${os_pretty}" ]]; then
    echo "- OS: ${os_pretty}"
  fi
fi

if [[ ${MODE_LINUX_X11} -eq 1 ]]; then
  echo
  echo "### Linux touchscreen (X11)"
  echo
  echo "Fill these in (manual):"
  echo "- Panel model:"
  echo "- Resolution:"
  echo "- Input device name(s):"
  echo "- Driver stack (X11):"
  echo "- Notes:"

  if have xrandr && [[ -n "${DISPLAY:-}" ]]; then
    print_cmd_block "xrandr --listmonitors" xrandr --listmonitors
  else
    echo
    echo "#### \`xrandr --listmonitors\`"
    echo "(skipped: xrandr not found or DISPLAY unset)"
  fi

  if have xinput && [[ -n "${DISPLAY:-}" ]]; then
    print_cmd_block "xinput list" xinput list
  else
    echo
    echo "#### \`xinput list\`"
    echo "(skipped: xinput not found or DISPLAY unset)"
  fi

  print_cmd_block "DRM connectors (sysfs: /sys/class/drm)" dump_drm_connectors

  if have edid-decode; then
    print_cmd_block "EDID decode (sysfs: /sys/class/drm/*/edid)" dump_drm_edid_decode
  else
    echo
    echo "#### \`EDID decode (sysfs: /sys/class/drm/*/edid)\`"
    echo "(skipped: edid-decode not found; install it for panel model details)"
  fi

  print_cmd_block "ls -l /dev/input/by-id and /dev/input/by-path" dump_input_symlinks
  print_cmd_block "udevadm info (touchscreen/tablet devices)" dump_udev_touch_devices

  if [[ -r /proc/bus/input/devices ]]; then
    print_cmd_block "/proc/bus/input/devices" dump_proc_bus_input_devices
  fi

  if have libinput; then
    if libinput list-devices >/dev/null 2>&1; then
      print_cmd_block "libinput list-devices" libinput list-devices
    else
      echo
      echo "#### \`libinput list-devices\`"
      echo "(skipped: libinput needs permissions; try: sudo libinput list-devices)"
    fi
  fi
fi
