#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: touch-infra/build-artifacts.sh [OPTIONS]

Builds installable artifacts using the wrapper repo (krita-docker-setup):
  - Linux AppImage (via ./bin/build_appimage)
  - Android APK (via ./bin/build-krita-android)

Options:
  --docker-setup <path>     Path to krita-docker-setup checkout.
                            If omitted, auto-detect when this repo lives at:
                              <krita-docker-setup>/persistent/krita
  --jobs <N>                Parallel jobs for Linux build (default: nproc).
  --appimage                Build Linux AppImage.
  --android-apk             Build Android APK (arm64-v8a debug).
  --android-container <c>   Android container name (default: krita-android-1).
  --apply-wrapper-patches   Apply touch-infra/krita-docker-env-patches first.

Examples:
  touch-infra/build-artifacts.sh --appimage
  touch-infra/build-artifacts.sh --android-apk --android-container krita-android-1
  touch-infra/build-artifacts.sh --appimage --android-apk --apply-wrapper-patches
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DOCKER_SETUP_DIR="${KRITA_DOCKER_SETUP_DIR:-}"
JOBS="$(nproc)"
DO_APPIMAGE=0
DO_ANDROID=0
ANDROID_CONTAINER="krita-android-1"
APPLY_WRAPPER_PATCHES=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --docker-setup)
      shift
      DOCKER_SETUP_DIR="${1:-}"
      ;;
    --jobs)
      shift
      JOBS="${1:-}"
      ;;
    --appimage)
      DO_APPIMAGE=1
      ;;
    --android-apk)
      DO_ANDROID=1
      ;;
    --android-container)
      shift
      ANDROID_CONTAINER="${1:-}"
      ;;
    --apply-wrapper-patches)
      APPLY_WRAPPER_PATCHES=1
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [[ ${DO_APPIMAGE} -eq 0 && ${DO_ANDROID} -eq 0 ]]; then
  echo "error: pick at least one of --appimage or --android-apk" >&2
  usage >&2
  exit 2
fi

if [[ -z "${DOCKER_SETUP_DIR}" ]]; then
  CANDIDATE="$(cd "${REPO_ROOT}/../.." && pwd)"
  if [[ -x "${CANDIDATE}/bin/build-krita" && -d "${CANDIDATE}/.git" ]]; then
    DOCKER_SETUP_DIR="${CANDIDATE}"
  else
    echo "error: could not auto-detect krita-docker-setup. Set KRITA_DOCKER_SETUP_DIR or pass --docker-setup." >&2
    exit 2
  fi
fi

if [[ ! -x "${DOCKER_SETUP_DIR}/bin/build-krita" ]]; then
  echo "error: ${DOCKER_SETUP_DIR} does not look like krita-docker-setup (missing bin/build-krita)" >&2
  exit 2
fi

if [[ ${APPLY_WRAPPER_PATCHES} -eq 1 ]]; then
  "${SCRIPT_DIR}/apply-krita-docker-env-patches.sh" --docker-setup "${DOCKER_SETUP_DIR}"
fi

if [[ ${DO_APPIMAGE} -eq 1 ]]; then
  (
    cd "${DOCKER_SETUP_DIR}"
    ./bin/build_image --network host krita-deps
    ./bin/build-krita --no-tests --jobs "${JOBS}"
    ./bin/build_appimage --jobs "${JOBS}"
  )

  echo "AppImage output:"
  ls -la "${DOCKER_SETUP_DIR}/persistent/"*.AppImage "${DOCKER_SETUP_DIR}/persistent/"*.appimage 2>/dev/null || true
fi

if [[ ${DO_ANDROID} -eq 1 ]]; then
  (
    cd "${DOCKER_SETUP_DIR}"
    ./bin/build_image --android --network host krita-android
    ./bin/build-krita-android --container "${ANDROID_CONTAINER}" --abi arm64-v8a --package-type debug --jobs "$(nproc)"
  )

  echo "APK output:"
  ls -la "${DOCKER_SETUP_DIR}/persistent/wd/krita/_packaging/"*.apk 2>/dev/null || true
fi
