#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: touch-infra/apply-krita-docker-env-patches.sh [--docker-setup <path>]

Applies the patch series in touch-infra/krita-docker-env-patches/ onto a local
checkout of the wrapper repo (krita-docker-setup).

Defaults:
  --docker-setup is auto-detected when this repo is checked out at:
    <krita-docker-setup>/persistent/krita

Notes:
  - Refuses to run if the wrapper repo has a dirty working tree.
  - Skips patches whose subject already exists in the wrapper repo history.
EOF
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PATCH_DIR="${SCRIPT_DIR}/krita-docker-env-patches"

DOCKER_SETUP_DIR="${KRITA_DOCKER_SETUP_DIR:-}"

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
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

if [[ -z "${DOCKER_SETUP_DIR}" ]]; then
  CANDIDATE="$(cd "${REPO_ROOT}/../.." && pwd)"
  if [[ -x "${CANDIDATE}/bin/build-krita" && -d "${CANDIDATE}/.git" ]]; then
    DOCKER_SETUP_DIR="${CANDIDATE}"
  else
    echo "error: could not auto-detect krita-docker-setup. Set KRITA_DOCKER_SETUP_DIR or pass --docker-setup." >&2
    exit 2
  fi
fi

if [[ ! -d "${DOCKER_SETUP_DIR}/.git" ]]; then
  echo "error: not a git repo: ${DOCKER_SETUP_DIR}" >&2
  exit 2
fi

if [[ ! -d "${PATCH_DIR}" ]]; then
  echo "error: patch directory not found: ${PATCH_DIR}" >&2
  exit 2
fi

if [[ -n "$(git -C "${DOCKER_SETUP_DIR}" status --porcelain=v1)" ]]; then
  echo "error: wrapper repo has uncommitted changes; aborting: ${DOCKER_SETUP_DIR}" >&2
  git -C "${DOCKER_SETUP_DIR}" status --porcelain=v1 >&2 || true
  exit 1
fi

shopt -s nullglob
patches=( "${PATCH_DIR}"/*.patch )
shopt -u nullglob

if [[ ${#patches[@]} -eq 0 ]]; then
  echo "error: no patches found in ${PATCH_DIR}" >&2
  exit 2
fi

for patch_path in "${patches[@]}"; do
  subject="$(
    sed -n 's/^Subject: //p' "${patch_path}" | head -n 1 | sed -E 's/^\\[PATCH[^]]*\\][[:space:]]*//'
  )"

  if [[ -z "${subject}" ]]; then
    echo "error: could not parse Subject from ${patch_path}" >&2
    exit 2
  fi

  if git -C "${DOCKER_SETUP_DIR}" log -n 1 --format=%s --grep "${subject}" --fixed-strings >/dev/null 2>&1; then
    echo "skip: ${subject}"
    continue
  fi

  echo "apply: ${subject}"
  git -C "${DOCKER_SETUP_DIR}" am --3way "${patch_path}"
done

echo "ok: patches applied (or already present)"
