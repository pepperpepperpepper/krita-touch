#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: touch-infra/collect-validation-device-info.sh [--linux-x11] [--android-adb] [--adb-serial <serial>] [--output <path>]

Collects basic host/display/input info that is useful for documenting the
primary manual-validation device in plan.md.

Notes:
  - This is best-effort and will skip commands that are unavailable.
  - For X11 probes (xrandr/xinput), you may need to set DISPLAY/XAUTHORITY if
    you run this from a TTY/SSH session. Use --display/--xauthority or env vars.
  - Some sections (like libinput) may require sudo on some distros.

Examples:
  touch-infra/collect-validation-device-info.sh --linux-x11
  touch-infra/collect-validation-device-info.sh --linux-x11 --display :0 --xauthority ~/.Xauthority
  touch-infra/collect-validation-device-info.sh --linux-x11 --output /tmp/device.md
  touch-infra/collect-validation-device-info.sh --android-adb
  touch-infra/collect-validation-device-info.sh --android-adb --adb-serial emulator-5554
EOF
}

MODE_LINUX_X11=0
MODE_ANDROID_ADB=0
OUTPUT_PATH=""
ADB_SERIAL=""
X11_DISPLAY_OVERRIDE=""
X11_XAUTHORITY_OVERRIDE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --linux-x11)
      MODE_LINUX_X11=1
      ;;
    --android-adb)
      MODE_ANDROID_ADB=1
      ;;
    --adb-serial)
      shift
      ADB_SERIAL="${1:-}"
      ;;
    --display)
      shift
      X11_DISPLAY_OVERRIDE="${1:-}"
      ;;
    --xauthority)
      shift
      X11_XAUTHORITY_OVERRIDE="${1:-}"
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

if [[ ${MODE_LINUX_X11} -eq 0 && ${MODE_ANDROID_ADB} -eq 0 ]]; then
  MODE_LINUX_X11=1
fi

if [[ -n "${OUTPUT_PATH}" ]]; then
  exec >"${OUTPUT_PATH}"
fi

have() { command -v "$1" >/dev/null 2>&1; }

UDEV_TOUCH_EVENT_HANDLERS=()

adb_cmd() {
  if [[ -n "${ADB_SERIAL}" ]]; then
    adb -s "${ADB_SERIAL}" "$@"
  else
    adb "$@"
  fi
}

ADB_SELECT_MSG=""
select_adb_device() {
  ADB_SELECT_MSG=""

  if ! have adb; then
    ADB_SELECT_MSG="adb not found; install Android platform-tools"
    return 1
  fi

  mapfile -t adb_rows < <(adb devices 2>/dev/null | awk 'NR>1 && $1 != "" {print $1 "\t" $2}')

  if [[ ${#adb_rows[@]} -eq 0 ]]; then
    ADB_SELECT_MSG="No adb devices found. Plug in a tablet (USB) or start an emulator, then re-run."
    return 1
  fi

  if [[ -n "${ADB_SERIAL}" ]]; then
    local row
    for row in "${adb_rows[@]}"; do
      local serial="${row%%$'\t'*}"
      local state="${row#*$'\t'}"
      if [[ "${serial}" == "${ADB_SERIAL}" ]]; then
        if [[ "${state}" == "device" ]]; then
          ADB_SELECT_MSG="Using adb serial: ${ADB_SERIAL}"
          return 0
        fi

        ADB_SELECT_MSG="Requested adb serial '${ADB_SERIAL}' is present but not usable (state=${state}).\n- If unauthorized: unlock tablet and accept the USB debugging prompt.\n- If offline: reconnect cable or restart adb server.\nThen re-run."
        return 1
      fi
    done

    local available=""
    for row in "${adb_rows[@]}"; do
      local serial="${row%%$'\t'*}"
      local state="${row#*$'\t'}"
      available+="- ${serial} (state=${state})\n"
    done
    ADB_SELECT_MSG="Requested adb serial '${ADB_SERIAL}' not found. Available devices:\n${available%\\n}"
    return 1
  fi

  if [[ ${#adb_rows[@]} -gt 1 ]]; then
    local available=""
    for row in "${adb_rows[@]}"; do
      local serial="${row%%$'\t'*}"
      local state="${row#*$'\t'}"
      available+="- ${serial} (state=${state})\n"
    done
    ADB_SELECT_MSG="Multiple adb devices detected. Re-run with: --adb-serial <serial>\nAvailable devices:\n${available%\\n}"
    return 1
  fi

  local only_serial="${adb_rows[0]%%$'\t'*}"
  local only_state="${adb_rows[0]#*$'\t'}"
  ADB_SERIAL="${only_serial}"

  if [[ "${only_state}" != "device" ]]; then
    ADB_SELECT_MSG="Found adb device '${ADB_SERIAL}' but it is not usable (state=${only_state}).\n- If unauthorized: unlock tablet and accept the USB debugging prompt.\n- If offline: reconnect cable or restart adb server.\nThen re-run."
    return 1
  fi

  ADB_SELECT_MSG="Using adb serial: ${ADB_SERIAL} (auto-selected)"
  return 0
}

print_cmd_block() {
  local cmd_label="$1"
  shift

  echo
  echo "#### \`${cmd_label}\`"
  echo '```text'
  if "$@" 2>&1; then
    :
  else
    echo "(command failed)"
  fi
  echo '```'
}

dump_android_summary() {
  if ! have adb; then
    echo "(skipped: adb not found; install Android platform-tools)"
    return 0
  fi

  if [[ -n "${ADB_SERIAL}" ]]; then
    echo "adb_serial: ${ADB_SERIAL}"
  else
    echo "adb_serial: (default)"
  fi

  echo
  echo "manufacturer: $(adb_cmd shell getprop ro.product.manufacturer 2>/dev/null | tr -d '\r' || true)"
  echo "model: $(adb_cmd shell getprop ro.product.model 2>/dev/null | tr -d '\r' || true)"
  echo "device: $(adb_cmd shell getprop ro.product.device 2>/dev/null | tr -d '\r' || true)"
  echo "android_release: $(adb_cmd shell getprop ro.build.version.release 2>/dev/null | tr -d '\r' || true)"
  echo "android_sdk: $(adb_cmd shell getprop ro.build.version.sdk 2>/dev/null | tr -d '\r' || true)"

  echo
  echo "wm_size:"
  adb_cmd shell wm size 2>/dev/null | tr -d '\r' | sed 's/^/  /' || true
  echo "wm_density:"
  adb_cmd shell wm density 2>/dev/null | tr -d '\r' | sed 's/^/  /' || true
}

dump_android_props() {
  if ! have adb; then
    echo "(skipped: adb not found; install Android platform-tools)"
    return 0
  fi

  adb_cmd shell getprop 2>/dev/null | tr -d '\r' || true
}

dump_android_dumpsys_input_head() {
  if ! have adb; then
    echo "(skipped: adb not found; install Android platform-tools)"
    return 0
  fi

  if have sed; then
    adb_cmd shell dumpsys input 2>/dev/null | tr -d '\r' | sed -n '1,200p' || true
  else
    adb_cmd shell dumpsys input 2>/dev/null | tr -d '\r' || true
  fi
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

X11_DISPLAY_EFFECTIVE="${X11_DISPLAY_OVERRIDE:-${DISPLAY:-}}"
X11_XAUTHORITY_EFFECTIVE="${X11_XAUTHORITY_OVERRIDE:-${XAUTHORITY:-}}"

echo "### Validation device info (generated)"
echo
echo "- Collected at: $(date -Iseconds)"
echo "- Host: $(hostname 2>/dev/null || echo unknown)"
echo "- User: $(id -un 2>/dev/null || echo unknown)"
echo "- Kernel: $(uname -srmo 2>/dev/null || uname -a)"
echo "- Session: \`XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-unknown}\`"
echo "- Desktop: \`XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-unknown}\`"
echo "- Display (env): \`DISPLAY=${DISPLAY:-<unset>}\`"
echo "- X11 probe: \`DISPLAY=${X11_DISPLAY_EFFECTIVE:-<unset>} XAUTHORITY=${X11_XAUTHORITY_EFFECTIVE:-<unset>}\`"

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

  x11_env=()
  if [[ -n "${X11_DISPLAY_EFFECTIVE}" ]]; then
    x11_env+=(DISPLAY="${X11_DISPLAY_EFFECTIVE}")
  fi
  if [[ -n "${X11_XAUTHORITY_EFFECTIVE}" ]]; then
    x11_env+=(XAUTHORITY="${X11_XAUTHORITY_EFFECTIVE}")
  fi

  if have xrandr && [[ -n "${X11_DISPLAY_EFFECTIVE}" ]]; then
    print_cmd_block "xrandr --listmonitors" env "${x11_env[@]}" xrandr --listmonitors
  else
    echo
    echo "#### \`xrandr --listmonitors\`"
    echo "(skipped: xrandr not found or DISPLAY unset; try: --display :0 --xauthority ~/.Xauthority)"
  fi

  if have xinput && [[ -n "${X11_DISPLAY_EFFECTIVE}" ]]; then
    print_cmd_block "xinput list" env "${x11_env[@]}" xinput list
  else
    echo
    echo "#### \`xinput list\`"
    echo "(skipped: xinput not found or DISPLAY unset; try: --display :0 --xauthority ~/.Xauthority)"
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

if [[ ${MODE_ANDROID_ADB} -eq 1 ]]; then
  echo
  echo "### Android tablet (adb)"
  echo
  echo "Fill these in (manual):"
  echo "- Manufacturer:"
  echo "- Model:"
  echo "- Android version:"
  echo "- Screen: (resolution / dpi)"
  echo "- Stylus: (yes/no + model)"
  echo "- Notes:"

  if have adb; then
    print_cmd_block "adb version" adb version
    print_cmd_block "adb devices -l" adb devices -l
  else
    echo
    echo "#### \`adb\`"
    echo "(skipped: adb not found; install Android platform-tools)"
  fi

  if select_adb_device; then
    echo
    echo "#### adb device selection"
    echo '```text'
    printf "%b\n" "${ADB_SELECT_MSG}"
    echo '```'

    print_cmd_block "adb device summary (getprop + wm)" dump_android_summary
    print_cmd_block "adb shell getprop (full)" dump_android_props
    print_cmd_block "adb shell dumpsys input (head -n 200)" dump_android_dumpsys_input_head
  else
    echo
    echo "#### adb device selection"
    echo '```text'
    printf "%b\n" "${ADB_SELECT_MSG}"
    echo '```'
    echo
    echo "(skipped: adb shell dumps; no usable device selected)"
  fi
fi
