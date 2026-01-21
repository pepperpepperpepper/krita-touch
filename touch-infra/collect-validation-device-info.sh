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

echo "### Validation device info (generated)"
echo
echo "- Collected at: $(date -Iseconds)"
echo "- Host: $(hostname 2>/dev/null || echo unknown)"
echo "- User: $(id -un 2>/dev/null || echo unknown)"
echo "- Kernel: $(uname -srmo 2>/dev/null || uname -a)"
echo "- Session: XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-unknown}"
echo "- Desktop: XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-unknown}"
echo "- Display: DISPLAY=${DISPLAY:-<unset>}"

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

  if [[ -r /proc/bus/input/devices ]]; then
    print_cmd_block "sed -n '1,200p' /proc/bus/input/devices" sed -n '1,200p' /proc/bus/input/devices
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

