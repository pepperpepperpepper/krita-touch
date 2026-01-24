# Procreate‑Inspired Krita (Touch‑First) — Plan

This document is the execution spec for a **Procreate‑inspired**, **touch‑first** fork of Krita.

Primary targets:

- **Linux desktop touchscreens** (**X11 first**, Wayland later)
- **Android tablets**

Primary UX rule:

- **Finger = gestures only** by default (no finger painting)
- **Pen = painting**

We drive the work via **small, shippable milestones**, each with:

- a **headless CI smoke test** (launch + deterministic screenshot + crash scan)
- a **manual acceptance checklist** on real touch hardware

---

## Table of Contents

- 0) Current Snapshot + Task Tracker (as of 2026‑01‑21)
- 1) Goals / Non‑Goals / Principles
- 2) Repo Layout + Branching
- 3) Build & Test Workflow (Docker)
- 4) Headless Smoke Tests (Linux Xvfb + Android Genymotion Cloud)
- 5) Procreate Reference Index (Docs)
- 6) Procreate Interaction Inventory → Krita Parity Map
- 7) Milestones (M0..)
- 8) Implementation Playbooks (P0/P1) — code checkpoints + acceptance
- 9) Open Questions

---

## 0) Current Snapshot + Task Tracker (as of 2026‑01‑21)

### Task Tracker (update this as we go)

Status legend:

- `[ ]` not started
- `[x]` done
- Use “(in progress)” inline when needed.

Current focus (prioritized): **Android tablet smoke + device validation**

- [x] **Infra** — Android tablet smoke suite on Genymotion (Nexus 10): `./bin/krita-gmsaas-smoke-touch` runs the full touch batch end-to-end (incl. `modify` + `top-bar-light`).
- [ ] **Infra** — Touch smoke test architecture v2 *(in progress)*: evolve `--touch-smoke` from “stable screenshots” into **assertive, cross-platform tests** (Linux Xvfb + Android/Genymotion) with reusable primitives + machine-readable results (not just pixels/PNG diffs).
- [x] **Infra** — UI smoke artifacts: every scenario run must produce a screenshot, and we should generate a shareable `index.html` gallery that embeds/links all screenshots (Linux + Android) and upload it via `wtf-upload` (in `$PATH`).
- [x] **P0** — Transform parity v1: pinch/rotate routing works reliably (inside transform box → transform; outside → canvas) on X11 + Android. *(Manual device validation tracked under Meta.)*
- [x] Smoke: extend `--touch-smoke=transform-tool` to apply a deterministic transform gesture and verify pixels change (with a safe fallback).
- [x] **P1** — Touch sidebar v1: Modify button = temporary eyedropper (press-and-hold) with touch painting override while held.
- [x] **P1** — ColorDrop v1: drag-fill threshold adjust UI (overlay) + persist chosen threshold to Fill Tool settings.
- [x] **P1** — Layers panel gestures v1: swipe left → Layer Options sheet; swipe right → multi-select; hold visibility icon → solo visibility.
- [ ] **Meta** — Pick + document primary validation devices (exact Linux touchscreen + Android tablet models) and run a full manual acceptance pass.
  - [ ] Document Linux touchscreen device + driver stack (X11): panel model, resolution, input device name.
    - Use `touch-infra/collect-validation-device-info.sh --linux-x11` and paste results into **Validation devices** below.
  - [x] Document Android tablet validation device (CI): Genymotion Cloud recipe `krita_tablet_nexus10_api35` (Android 15 / API 35 / 2560×1600 / dpi 320 / arm64). *(No stylus; emulator.)*
  - [ ] Run manual acceptance: Transform parity v1 (P0.3).
  - [ ] Run manual acceptance: Touch sidebar Modify (P1.1).
  - [ ] Run manual acceptance: ColorDrop v1 (P1.4).
  - [ ] Run manual acceptance: Layers panel gestures v1 (P1.5).

Recently completed:

- [x] **P0** — QuickShape v2: recognition + snapping for **circle/ellipse**
- [x] **P0** — QuickShape v2: recognition + snapping for **rectangle**
- [x] **P1** — QuickShape v2: recognition + snapping for **triangle**
- [x] **P1** — QuickShape v2: recognition + snapping for **polygon**
- [x] **P2** — QuickShape v2: “second-finger tap to perfect”
- [x] **P3** — QuickShape v2: “Edit Shape” affordance (handles / mode)
- [x] Smoke: extend `--touch-smoke=quickshape` (or add new scenarios) to cover at least one non-line snapped shape, deterministically.

Work log (append-only; newest first):

- **2026‑01‑24**:
  - Infra: Linux smoke runner extracts per-scenario `KRITA_TOUCH_SMOKE_JSON` into `smoke-*-report.json`. Android runner pulls the full JSON report from `files/touch-smoke-report.json` (written by Krita) to avoid logcat truncation. Screenshot gallery links reports alongside PNG + logs.
  - Infra: Linux Xvfb + Genymotion touch smoke batch suites now include key cross-platform `script:` scenarios (canvas-only, undo/redo gating, clipboard overlay gating, clear-layer scrub gating).
  - Fix: Android 3‑finger clear-layer scrub now clears the active layer via `KisToolUtils::clearImage` (tool-independent), so clear-layer gating tests pass cross-platform.
- **2026‑01‑23**:
  - Infra: smoke scenarios `gesture-controls` + `layer-options` now fill the canvas black for more readable UI screenshots (Actions sheet / Layer Options sheet).
  - Smoke: `--touch-smoke=gesture-controls` fullscreen/canvas-only (4-finger tap) now runs through the **input-manager path** by sending synthetic 4-finger tap events to the canvas (keeps a direct-action fallback for platforms where multi-touch injection is flaky).
  - Infra: added a **scriptable touch-smoke runner** (`--touch-smoke=script:<name>`) that loads JSON scripts from Qt resources (`:/touchsmoke/...`) and executes reusable primitives with per-step `KRITA_TOUCH_SMOKE_JSON` reporting; added the first script `canvas-only-toggle` and validated it on Linux Xvfb.
  - Infra: touch-smoke scripts now support layer-count assertions + undo/redo gesture steps; added script `undo-redo-layer-gating` (2/3-finger tap) for cross-platform gating coverage.
  - Infra: touch-smoke scripts now support drag-path gesture primitives + assertions (`overlayVisible`, pixel alpha range) with Qt touch injection first and a direct gesture-action fallback; added `image.paint_rect` seeding for deterministic pixel tests.
  - Infra: added scripts `clipboard-overlay-gating` (3-finger swipe down) and `clear-layer-scrub-gating` (3-finger scrub) for cross-platform Gesture Controls regression coverage.
  - Validation: Linux Xvfb `gesture-controls` scenario OK.
  - Validation: Linux Xvfb `script:undo-redo-layer-gating` scenario OK.
  - Validation: Linux Xvfb `script:clipboard-overlay-gating` scenario OK.
  - Validation: Linux Xvfb `script:clear-layer-scrub-gating` scenario OK.
- **2026‑01‑22**:
  - Smoke: `--touch-smoke=gesture-controls` undo/redo tap gestures now run through the **input-manager path** by sending synthetic multi-touch tap events to the canvas (exercises `KisInputManager` shortcut matching; keeps a direct-action fallback for flaky platforms).
  - Smoke: `--touch-smoke=gesture-controls` clear-layer scrub gating now runs through the **input-manager path** by sending synthetic touch events to the canvas (exercises `KisInputManager` shortcut matching; keeps a direct-action fallback for flaky platforms).
  - Smoke: `--touch-smoke=gesture-controls` rotate-with-pinch gating now runs through the **input-manager path** by sending synthetic touch events to the canvas (exercises `KisInputManager` shortcut matching).
  - Smoke: `--touch-smoke=quickmenu` now covers the gesture-path end-to-end: slide/highlight routing, release triggers slot action, and hold-on-slot opens QuickMenu Setup (config sheet).
  - Smoke: `--touch-smoke=gesture-controls` now covers Clear Layer scrub gating (enabled clears; disabled does nothing) on the smoke document.
  - Smoke: `--touch-smoke=copypaste` now asserts pixel-level Copy & Paste behavior (seeded red content appears in the new layer; outside-selection stays transparent; source layer unchanged).
  - Validation: Linux Xvfb `copypaste` scenario OK.
  - Validation: Linux Xvfb `quickmenu` + `gesture-controls` scenarios both OK.
  - Committed: clear-layer scrub input-manager smoke (`48329596a8`).
  - Committed: touch-smoke test expansion (`673d39dbf1`).
- **2026‑01‑21**:
  - Android: rebuilt arm64-v8a debug APK + ran full Genymotion touch batch (16 scenarios) on Nexus 10 recipe (all OK).
  - Genymotion Cloud: switched canonical tablet recipe to `krita_tablet_nexus10_api35` (Nexus 10 2560×1600, Android 15 / API 35 / arm64); ran Android smoke `top-bar` (OK).
  - Smoke: `--touch-smoke` now emits a machine-readable per-step report line `KRITA_TOUCH_SMOKE_JSON ...` alongside `KRITA_TOUCH_SMOKE_DONE ...`.
  - Smoke: `--touch-smoke=layers-panel` now asserts swipe-right multi-select **and** hold-visibility “solo” + restore.
  - Smoke: `--touch-smoke=gesture-controls` now asserts rotate-with-pinch, clipboard, undo/redo, quickmenu, and fullscreen gating (enabled does the thing; disabled does not) and still opens the Gestures page for the screenshot.
  - Repo status note (as of 2026‑01‑21): local WIP changes were uncommitted / not pushed.
- **2026‑01‑20**:
  - Android smoke: added `./bin/krita-gmsaas-smoke-touch` (tablet recipe) + improved `./bin/gmsaas-smoke-android` robustness (wait for instance `ONLINE`, log instance UUID early); ran Genymotion scenario `top-bar` (OK).
  - Smoke: rebuilt + ran xvfb scenarios `colordrop`, `layers-panel`, `layer-options`, and reran `transform-tool` (all OK).
  - Transform parity v1: refine touch routing to require 2-of-3 hit-tests (p0/p1/center) + ignore Released/degenerate touch updates when delegating to transform; rebuilt + ran xvfb `--scenario transform-tool` (OK).
  - Smoke: added `--touch-smoke=modify` to exercise the TouchDocker Modify hold (Color Sampler) and validate tool + `touchPainting` restore; rebuilt + ran xvfb scenario (OK).
  - Smoke: `--touch-smoke=transform-tool` applies a deterministic touch transform gesture and verifies the image projection changes (with fallbacks); rebuilt + ran xvfb scenario (OK).
  - Transform parity v1: improved transform bounds hit-test (widget-space + 16px margin) to reduce edge misroutes; rebuilt + ran `--touch-smoke=transform-tool` (OK). Manual X11+Android validation pending.
  - Committed WIP: `krita-docker-setup` (`105839f`) + Krita fork (`1321859542`).
  - Linux smoke suite rerun: `./bin/krita-xvfb-smoke-touch --wait 10` (all scenarios OK).
  - Smoke: `--touch-smoke=quickshape` draws a deterministic non-line shape (rectangle stroke + fallback direct rectangle paint).
  - QuickShape v2: “Edit Shape” mode (contextual “Edit Shape” popup + draggable on-canvas handles; tap outside to commit).
  - QuickShape v2: second-finger tap to “perfect” snapped shapes (circle, square, regular polygon, 45° line).
  - QuickShape v2: draw-and-hold polygon snapping (corner-detected; up to 10 sides).
  - QuickShape v2: draw-and-hold triangle snapping (3-point polygon).
  - QuickShape v2: draw-and-hold rectangle snapping (axis-aligned).
  - QuickShape v2: draw-and-hold ellipse/circle snapping (circle snaps to a true circle if aspect ratio is close).
  - Linux smoke suite includes `top-bar-light` and validates `KRITA_TOUCH_SMOKE_DONE ... status=OK`.
  - AppImage built and smoke suite verified against the produced AppImage.

### Host + constraints

- Host OS: **Arch Linux**
- Docker is the primary build environment.
- AWS VM reality:
  - Often **no `/dev/kvm`** → local Android emulators are not practical.
    - Quick check: `ls -l /dev/kvm` (missing = no hardware acceleration)
    - If we truly need KVM on AWS: use an **EC2 bare metal** instance type (`*.metal`); otherwise use Genymotion Cloud.
  - This machine has **low CPU** (`nproc=2`) → clean Linux builds can take **hours**.
- Android CI emulator strategy: **Genymotion Cloud (SaaS)**
  - Genymotion Cloud CLI (`gmsaas`) installed via **pipx** (preferred on Arch):

    ```bash
    export PIPX_HOME="$HOME/.local/pipx"
    export PIPX_BIN_DIR="$HOME/.local/bin"
    pipx install gmsaas
    ```

    Notes:

    - If your `pipx` default points at an unwritable path (example: `/mnt/extra/pipx`), export `PIPX_HOME` / `PIPX_BIN_DIR` as above.
- Genymotion API token stored locally in `/home/arch/.api-keys` (**never commit**).

### Validation devices (primary)

This section is the canonical record of the primary touch devices used for manual acceptance.

#### Linux touchscreen (X11)

- Panel model:
- Resolution:
- Input device name(s):
- Driver stack (X11):
- Notes:

Collected output (paste from `touch-infra/collect-validation-device-info.sh --linux-x11`):

```text
<paste here>
```

#### Android tablet (CI / smoke)

- Genymotion Cloud recipe: `krita_tablet_nexus10_api35` (Android 15 / API 35 / 2560×1600 / dpi 320 / arm64).
- Notes: No stylus; emulator.

### Manual acceptance log (Meta)

Record each manual acceptance pass here so we can safely check off the Meta tasks above.

Template:

- Date:
- Device:
- Build (commit/AppImage/APK):
- Results:
  - P0.3 Transform parity v1: PASS | FAIL | N/A — notes
  - P1.1 Touch sidebar Modify: PASS | FAIL | N/A — notes
  - P1.4 ColorDrop v1: PASS | FAIL | N/A — notes
  - P1.5 Layers panel gestures v1: PASS | FAIL | N/A — notes
- Notes:

### Two repos (important)

We work in two git repos:

1) Docker wrapper repo (scripts + Dockerfile)

- `~/dev/krita/krita-docker-setup/`

2) Krita fork (actual product changes)

- `~/dev/krita/krita-docker-setup/persistent/krita/`

This plan lives in the Krita fork:

- Source of truth: `~/dev/krita/krita-docker-setup/persistent/krita/plan.md`
- Convenience symlink: `~/dev/krita/krita-docker-setup/plan.md` → `persistent/krita/plan.md`

### Branch snapshots (as of 2026‑01‑20)

- Docker wrapper repo (`~/dev/krita/krita-docker-setup/`):
  - upstream: `https://invent.kde.org/dkazakov/krita-docker-env.git`
  - local branch: `infra/smoke-tests`
  - local HEAD commit: `c7d7b5a` (adds AppImage support for Linux Xvfb smoke scripts)
  - patches tracked in this repo: `touch-infra/krita-docker-env-patches/*.patch` (apply with `git am`)
- Krita fork (`~/dev/krita/krita-docker-setup/persistent/krita/`):
  - GitHub: `git@github.com:pepperpepperpepper/krita-touch.git`
  - branch: `touch/procreate-mvp`
  - local HEAD commit: `61a560a7af` (`touch-infra: update krita-docker-env patchset`)
  - feature baseline commit: `bb8e957946` (`touch: procreate-mvp baseline`)

### Present in working tree (WIP)

These items exist locally and should be treated as our baseline direction:

- Touch-first defaults:
  - `krita/data/kritarc` includes:
    - `touchPainting=2` (disable touch painting)
    - `currentInputProfile=Touch Gestures Only`
    - `touchModeEnabled=true`
  - `krita/data/input/touch-gestures-only.profile`
  - `krita/data/input/CMakeLists.txt` installs the profile
- Touch Mode toggle:
  - config: `touchModeEnabled` in `libs/ui/kis_config.{h,cc}`
  - menu action: `touch_mode_enabled` in `libs/ui/KisMainWindow.cpp`
  - applies workspace + shows Touch Docker
  - applies touch-first chrome:
    - hides desktop chrome (menu bar, status bar, non-touch toolbars)
    - switches to touch theme:
      - default: **Touch Procreate Dark** (`krita/data/themes/TouchProcreateDark.colors`)
      - optional: **Touch Procreate Light** (`krita/data/themes/TouchProcreateLight.colors`, toggle `touch_theme_light`, config `touchThemeName`)
      - install: `krita/data/themes/CMakeLists.txt`
- Deterministic “touch smoke scenario” hook:
  - Desktop: `--touch-smoke=<scenario>` or env `KRITA_TOUCH_SMOKE=<scenario>`
  - Android: intent extra `KRITA_TOUCH_SMOKE=<scenario>` forwarded into the same hook
  - code:
    - `libs/ui/KisApplicationArguments.{h,cpp}`
    - `libs/ui/KisApplication.{h,cpp}`
    - `krita/main.cc`
    - `packaging/android/apk/src/org/krita/android/{MainActivity,JNIWrappers}.java`
- Touch-first selection tool (Procreate-inspired unified selection tool):
  - `plugins/tools/selectiontools/kis_tool_select_touch.{h,cc}`
  - `plugins/tools/selectiontools/KisToolSelectTouch.action`
  - registered via `plugins/tools/selectiontools/selection_tools.cc`
  - includes touch-first selection controls (Selection method + Selection action + Feather slider)
  - includes a touch-friendly “Operations” section (Invert / Deselect / Color Fill / Clear / Copy & Paste / Cut & Paste)
  - includes touch-friendly Save/Load selection (single-slot, in-memory)
  - `--touch-smoke=selection-tool` now also shows the Tool Options docker (`sharedtooldocker`) so screenshots capture the UI
- Touch workspaces (placeholders for now):
  - `krita/data/workspaces/Touch_Procreate_like.kws`
  - `krita/data/workspaces/Touch_Procreate_like_Right.kws`
  - installed via `krita/data/workspaces/CMakeLists.txt`
- Headless Android smoke helper (wrapper repo):
  - `~/dev/krita/krita-docker-setup/bin/gmsaas-smoke-android` (supports `--scenario`)
  - `~/dev/krita/krita-docker-setup/bin/krita-gmsaas-smoke-touch` (touch-first batch runner; defaults to the canonical tablet recipe)
  - supports multiple `--scenario` in one instance + fails on `FATAL EXCEPTION`/`ANR in <pkg>` in logcat
  - Example artifact: `persistent/smoke-android-krita-selection-tool.png` + `persistent/smoke-android-krita-selection-tool-logcat.txt`
  - Validation (2026‑01‑17): batch scenarios succeeded on Genymotion Cloud for:
    - `top-bar`, `touch-sidebar`, `selection-tool`, `transform-tool`, `layers-panel`, `layer-options`, `color-panel`,
      `colordrop`, `actions-sheet`, `gesture-controls`, `quickmenu`, `quickmenu-setup`, `copypaste`
  - Validation (2026‑01‑21): full touch batch succeeded on Genymotion Cloud (Nexus 10 / 2560×1600 / API 35) for:
    - `top-bar`, `top-bar-light`, `touch-sidebar`, `modify`, `selection-tool`, `transform-tool`, `layers-panel`, `layer-options`, `color-panel`,
      `colordrop`, `quickshape`, `actions-sheet`, `gesture-controls`, `quickmenu`, `quickmenu-setup`, `copypaste`
  - As of 2026‑01‑17: scenario runs wait for a logcat marker (instead of fixed sleeps):
    - `KRITA_TOUCH_SMOKE_DONE scenario=<name> status=<OK|ERROR>`
    - This reduces “splash screen / Android launcher” false screenshots.
- Background build helper (wrapper repo):
  - `~/dev/krita/krita-docker-setup/bin/bg-build-krita` (detached `tmux` + timestamped log, symlink `persistent/build-linux.log`)
- Linux headless screenshot runner (wrapper repo):
  - `~/dev/krita/krita-docker-setup/bin/krita-xvfb-screenshot`
  - `~/dev/krita/krita-docker-setup/bin/krita-xvfb-smoke-touch` (batch scenarios)
  - Important: it now correctly forwards `--touch-smoke=<scenario>` into Krita (previously args were lost inside nested `bash -lc`)
  - Validation (2026‑01‑17): `./bin/krita-xvfb-smoke-touch` produced non-empty PNGs for all default scenarios
  - Validation (2026‑01‑19): AppImage smoke suite succeeded for all default scenarios:
    - `./bin/krita-xvfb-smoke-touch --appimage persistent/krita-5.3.0-prealpha-9ea56e7bd4-x86_64.AppImage --wait 45`
    - outputs: `persistent/smoke-linux-appimage-*.png` + `persistent/smoke-linux-appimage-*-log.txt`
- Procreate-like touch sidebar (implemented by evolving Krita’s existing Touch Docker plugin):
  - `plugins/dockers/touchdocker/TouchDockerWidget.{ui,h,cpp}`
  - Large Undo/Redo + vertical Size/Opacity sliders + “Modify” button (mapped to Color Sampler tool)
  - Actions button (opens Procreate-like Actions sheet overlay; v1)
- Procreate-like “Gesture Controls” toggles (v1):
  - Implemented as a new **Gestures** page in the touch Actions sheet.
  - Backed by config keys in `libs/ui/kis_config.{h,cc}`:
    - `touchRotateWithPinchEnabled`
    - `touchQuickPinchToFitEnabled`
    - `touchQuickShapeEnabled`
    - `touchQuickMenuEnabled`
    - `touchClipboardGestureEnabled`
    - `touchClearLayerGestureEnabled` (3-finger scrub to clear layer)
    - `touchUndoRedoGesturesEnabled`
    - `touchFullscreenGestureEnabled`
  - Gesture gating (so toggles actually change behavior):
    - `libs/ui/input/kis_zoom_and_rotate_action.cpp` (rotate-with-pinch + quick pinch fit)
    - `libs/ui/input/KisTouchQuickMenuAction.cpp` (QuickMenu enable)
    - `libs/ui/input/KisTouchGestureAction.cpp` (Undo/Redo, clipboard, scrub clear, canvas-only)
- Copy/Paste overlay (Procreate 3-finger swipe down; v1):
  - overlay widget: `libs/ui/widgets/kis_touch_copypaste_overlay.{h,cpp}`
  - action id: `touch_copypaste_overlay` in `libs/ui/KisMainWindow.cpp`
  - gesture binding: `ThreeFingerDrag` → Copy/Paste overlay in `krita/data/input/touch-gestures-only.profile`
    - gated to mostly-vertical **downward swipe** in `libs/ui/input/KisTouchGestureAction.cpp`
  - headless smoke: `--touch-smoke=copypaste` (shows overlay)
- QuickMenu overlay (Procreate 1-finger hold; v1):
  - hold action: `libs/ui/input/KisTouchQuickMenuAction.{h,cpp}` (shows on `begin()`, selects on slide, triggers on release)
  - overlay widget: `libs/ui/widgets/kis_touch_quickmenu_overlay.{h,cpp}` (input-transparent; highlighting driven by the action)
  - slots are configurable (v1.1):
    - config persistence: `libs/ui/kis_config.{h,cc}` (`touchQuickMenuActionIds`)
    - touch UI: `libs/ui/widgets/kis_touch_quickmenu_config_sheet.{h,cpp}`
    - action id: `touch_quickmenu_configure` in `libs/ui/KisMainWindow.cpp` (reachable via Actions sheet → Gestures → “QuickMenu Setup”)
  - gesture binding: `OneFingerHold` → `Touch QuickMenu` in `krita/data/input/touch-gestures-only.profile`
  - headless smoke: `--touch-smoke=quickmenu` (shows overlay)
- Layer Options sheet (Procreate layer menu analog; v1):
  - overlay widget: `libs/ui/widgets/kis_touch_layer_options_sheet.{h,cpp}`
  - action id: `touch_layer_options_sheet` in `libs/ui/KisMainWindow.cpp`
  - headless smoke: `--touch-smoke=layer-options` (shows overlay)
- Deterministic “touch smoke scenario” screenshots now:
  - hide all dock widgets first (so user workspaces don’t affect screenshots)
  - show specific dockers by **factory id** (examples: `TouchDocker`, `KisLayerBox`, `ColorSelectorNg`)
  - Layers smoke scenarios now create extra layers for a stable “list” screenshot:
    - `--touch-smoke=layers-panel` / `--touch-smoke=layer-options`
    - uses action `add_new_paint_layer` (see `libs/ui/KisApplication.cpp`)
  - Actions sheet smoke:
    - `--touch-smoke=actions-sheet` (shows the Actions sheet overlay)
    - `--touch-smoke=gesture-controls` (shows the Actions sheet on the Gestures page)

---

## 1) Goals / Non‑Goals / Principles

### 1.1 Goals

- Deliver a **Procreate‑inspired** UI + touch interaction model for Krita with close-to-parity “feel”.
- Prioritize touch workflows:
  - **Finger-only mode** (navigation + commands)
  - **Stylus paints**
- Make it shippable:
  - small increments
  - CI-friendly smoke checks
  - manual touch acceptance per milestone

### 1.2 Non‑Goals (explicitly out-of-scope unless re-scoped)

- Full feature parity with Procreate (timelapse, 3D, etc.).
- Wayland-first tuning.
- Apple Pencil hardware-only features beyond generic stylus button mapping.

### 1.3 Principles

- **Touch targets**: minimum 48×48 px (prefer 56×56 for primary actions).
- **One-tap reachability**: core actions must not require menus/toolbars optimized for mouse.
- **Predictability** over “cleverness”: gesture routing must not fight the user.
- Prefer:
  - mapping to existing Krita actions
  - using workspaces/themes for visual changes
  - adding small, well-scoped overlay widgets for touch-only chrome

---

## 2) Repo Layout + Branching

### 2.1 Branching strategy (two repos)

- Krita fork branches (feature work):
  - `touch/procreate-mvp`
  - `ui/procreate-skin`
  - `tool/select-touch-v2`
  - `input/gesture-routing-x11`

- Docker wrapper branches (infra-only):
  - `infra/smoke-tests`
  - `infra/android-gmsaas`

If a change spans both repos, keep branch names aligned and document both commit SHAs in the PR.

### 2.2 Git hygiene

Track in Krita fork:

- `plan.md`
- touch defaults:
  - `krita/data/kritarc`
  - `krita/data/input/touch-gestures-only.profile`
- touch selection tool:
  - `plugins/tools/selectiontools/kis_tool_select_touch.{h,cc}`
  - `plugins/tools/selectiontools/KisToolSelectTouch.action`
- touch workspaces:
  - `krita/data/workspaces/Touch_Procreate_like*.kws`

Never commit:

- `/home/arch/.api-keys` (Genymotion token store)
- build outputs (`build-debug/`, Android `_build/`, AppImage artifacts, etc.)

---

## 3) Build & Test Workflow (Docker)

This project is built via `krita-docker-setup` because it mirrors KDE CI/AppImage build environments and makes headless testing reproducible.

### 3.1 Bootstrap deps (required before building the Docker image)

From `~/dev/krita/krita-docker-setup/`:

```bash
./bin/bootstrap-deps.sh
```

This produces `persistent/deps/_install/` used by the Dockerfile.

### 3.2 Build Docker images

Linux/AppImage base:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/build_image --network host krita-deps
```

Android base:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/build_image --android --network host krita-android
```

### 3.3 Run containers

Linux/AppImage container:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/run_container --network host krita-deps
```

Android container:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/run_container --network host krita-android
```

Enter shell:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/enter
```

### 3.4 Build Krita (Linux)

Preferred (host command; builds in container):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/build-krita -j"$(nproc)" --no-tests
```

#### Long-running builds: run in background

On this host (`nproc=2`), a clean build can take **hours**. Don’t run it in the foreground.

Preferred helper (starts a detached `tmux` session + writes a timestamped log):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/bg-build-krita -- --no-tests -j"$(nproc)"

# watch progress
tail -f persistent/build-linux.log
```

Manual `tmux` + log file:

```bash
cd ~/dev/krita/krita-docker-setup
tmux new -d -s krita-build-linux './bin/build-krita -j"$(nproc)" --no-tests 2>&1 | tee persistent/build-linux.log'

# watch progress
tail -f persistent/build-linux.log
```

Stop:

```bash
tmux kill-session -t krita-build-linux
```

Why it’s slow here:

- You’re effectively compiling a very large C++/Qt/KDE app on **2 vCPUs**.
- After a clean build dir wipe, the compiler has to rebuild everything.

Make iteration faster:

- Keep `/home/appimage/appimage-workspace/krita-build/` (don’t wipe it).
- Enable and persist compiler cache (`ccache`) to `/home/appimage/persistent/ccache`.
- If we want “fast clean builds”, move to an 8–16 vCPU instance.

### 3.5 Desktop testing (Linux)

Interactive GUI (X11):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/enter
krita
```

Headless smoke (CI-friendly):

- Run Krita under Xvfb:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb -- --version
```

- Take a screenshot artifact:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-screenshot --output=smoke-linux-base.png --wait=20
test -s persistent/smoke-linux-base.png
```

Limitations:

- Xvfb does **not** validate real multi-touch. It validates “launches + renders + no crash”.

### 3.6 Build Krita (Android)

We mirror Krita CI scripts. Use the Android container.

Recommended (host command; builds in Android container):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/build-krita-android --container krita-android-1 --abi arm64-v8a --package-type debug --jobs "$(nproc)"

# output:
ls -la persistent/wd/krita/_packaging/*.apk
```

Notes:

- Use `docker ps` to find the Android container name if it differs.
- The helper writes build folders under `persistent/wd/krita/` (can be huge).
- If disk is tight, keep `_packaging/*.apk` and delete `_build/`, `_install/`, `_staging/`.

#### Long-running Android builds: run in background

On this host (`nproc=2`), Android builds can take **hours**. Prefer a detached `tmux` build + log.

```bash
cd ~/dev/krita/krita-docker-setup
./bin/bg-build-krita-android -- --container krita-android-1 --abi arm64-v8a --package-type debug --jobs "$(nproc)"

# watch progress
tail -f persistent/build-android.log
```

Inside container:

```bash
. /var/lib/ci-files/setup-environment.sh
export KDECI_ANDROID_ABI=arm64-v8a
export KDECI_WORKDIR_PATH=/home/appimage/persistent/wd
```

Build via CI utilities (see `build-tools/ci-scripts/android.yml` and `build-tools/ci-scripts/build-android-package.py`).

Disk reality:

- Android workdirs can be huge (`_build/`, `_install/`, `_staging/`).
- Keep `_packaging/*.apk` and delete the rest when space is tight.

### 3.7 Build installable artifacts (AppImage + APK)

This repo includes a small helper that calls into `krita-docker-setup` to build
installable artifacts.

From `~/dev/krita/krita-docker-setup/persistent/krita/`:

```bash
# Linux AppImage (output: ~/dev/krita/krita-docker-setup/persistent/*.AppImage)
touch-infra/build-artifacts.sh --appimage

# Android debug APK (output: ~/dev/krita/krita-docker-setup/persistent/wd/krita/_packaging/*.apk)
touch-infra/build-artifacts.sh --android-apk --android-container krita-android-1
```

If this repo is *not* checked out inside `krita-docker-setup/persistent/krita`,
set `KRITA_DOCKER_SETUP_DIR=/path/to/krita-docker-setup`.

If your wrapper repo needs our infra fixes (non-hanging Android smoke, etc.),
apply the patch series first:

```bash
touch-infra/apply-krita-docker-env-patches.sh
```

---

## 4) Headless Smoke Tests (Linux + Android)

### 4.1 Deterministic UI states via `--touch-smoke`

To take stable screenshots of specific UI surfaces, we use a deterministic startup hook.

Entry points:

- Desktop:
  - `krita --touch-smoke=<scenario>`
  - OR env: `KRITA_TOUCH_SMOKE=<scenario>`
- Android:
  - intent extra: `KRITA_TOUCH_SMOKE=<scenario>` (wired through `MainActivity`)

Scenario names (keep stable):

- `top-bar` *(shows Touch Mode chrome/top bar)*
- `top-bar-light` *(shows Touch Mode chrome/top bar in Touch Procreate Light theme)*
- `selection-tool` *(creates a polygon selection, exercises Save/Load + Fill, and validates pixels)*
- `transform-tool`
- `touch-sidebar`
- `modify` *(exercises TouchDocker Modify (temporary Color Sampler) and validates restore behavior)*
- `layers-panel` *(populates layers and validates swipe-right multi-select + hold-visibility solo/restore)*
- `layer-options` *(validates swipe-left opens the touch Layer Options sheet)*
- `color-panel` *(currently opens existing color selector docker as placeholder)*
- `colordrop` *(performs a ColorDrop fill on the canvas)*
- `quickshape` *(draws a stroke + triggers QuickShape snapping)*
- `actions-sheet` *(opens the touch Actions sheet)*
- `gesture-controls` *(validates rotate-with-pinch gating and opens the Actions sheet on the Gestures page)*
- `quickmenu` *(opens QuickMenu, triggers Select slot, validates tool switch)*
- `quickmenu-setup` *(opens the touch QuickMenu Setup sheet)*
- `copypaste` *(creates selection, copies to new layer, and shows the touch Copy/Paste overlay)*

Script scenarios:

- `script:<name>` (or `script=<name>`) loads a JSON touch-smoke script from Qt resources (`:/touchsmoke/scripts/<name>.json`) and runs it with per-step `KRITA_TOUCH_SMOKE_JSON` reporting.
- Currently shipped scripts:
  - `script:canvas-only-toggle`
  - `script:undo-redo-layer-gating`
  - `script:clipboard-overlay-gating`
  - `script:clear-layer-scrub-gating`

Notes:

- Docker IDs in smoke scenarios refer to `KoDockFactoryBase::id()` (example: the Touch sidebar docker id is `TouchDocker`).
- The Krita side accepts some aliases (`touch_sidebar`, `touchdocker`, etc.), but CI should use the stable names above.
- Screenshot readability: `gesture-controls` and `layer-options` are hard to see on a light/white canvas. For smoke runs (and especially the screenshot gallery), ensure the document background/content is **black** before capturing (these scenarios should fill the canvas black automatically).
- Android realism note: Qt synthetic mouse/drag events can be unreliable on some Android builds.
  - To keep CI deterministic, smoke scenarios like `colordrop`, `quickshape`, `transform-tool` include a fallback that paints/fills directly if the input-event path doesn’t visibly change the layer.

### 4.2 Linux smoke commands

Notes:

- `./bin/krita-xvfb-screenshot` writes a per-run log file (default: `<output>-log.txt`).
- `./bin/krita-xvfb-screenshot` also extracts `KRITA_TOUCH_SMOKE_JSON ...` into a per-run report file (default: `<output>-report.json` when `<output>` ends with `.png`).
- When running a `--touch-smoke=<scenario>` it will wait (up to `--wait`) for the marker:
  - `KRITA_TOUCH_SMOKE_DONE scenario=<name> status=<OK|ERROR>`
  - This makes CI faster + avoids “half-painted” UI screenshots.

Examples:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-screenshot --output=smoke-linux-selection-tool.png --log=smoke-linux-selection-tool-log.txt --wait=20 -- --touch-smoke=selection-tool
test -s persistent/smoke-linux-selection-tool.png
test -s persistent/smoke-linux-selection-tool-log.txt
```

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-screenshot --output=smoke-linux-touch-sidebar.png --log=smoke-linux-touch-sidebar-log.txt --wait=8 -- --touch-smoke=touch-sidebar
test -s persistent/smoke-linux-touch-sidebar.png
test -s persistent/smoke-linux-touch-sidebar-log.txt
```

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-screenshot --output=smoke-linux-layers-panel.png --log=smoke-linux-layers-panel-log.txt --wait=8 -- --touch-smoke=layers-panel
test -s persistent/smoke-linux-layers-panel.png
test -s persistent/smoke-linux-layers-panel-log.txt
```

Batch run (recommended CI):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-smoke-touch --wait=8
```

Run against a shipped AppImage:

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-screenshot --appimage persistent/krita-5.3.0-prealpha-9ea56e7bd4-x86_64.AppImage --output=smoke-linux-appimage-top-bar.png --log=smoke-linux-appimage-top-bar-log.txt --wait=20 -- --touch-smoke=top-bar
test -s persistent/smoke-linux-appimage-top-bar.png
test -s persistent/smoke-linux-appimage-top-bar-log.txt
```

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-xvfb-smoke-touch --appimage persistent/krita-5.3.0-prealpha-9ea56e7bd4-x86_64.AppImage --wait=45
```

### 4.3 Upload screenshots (wtf-upload)

`wtf-upload` is available in `$PATH` and uploads files to S3, printing a public `https://` URL per file.

Examples:

```bash
cd ~/dev/krita/krita-docker-setup
wtf-upload persistent/smoke-linux-krita-*.png
wtf-upload persistent/smoke-linux-krita-*-log.txt
wtf-upload persistent/smoke-linux-appimage-*.png
wtf-upload persistent/smoke-linux-appimage-*-log.txt
wtf-upload persistent/smoke-android-krita-*.png
wtf-upload persistent/smoke-android-krita-*-logcat.txt
wtf-upload persistent/smoke-*-report.json
```

#### 4.3.1 Screenshot gallery (index.html)

Goal: make “UI smoke” easy to review by producing **one URL** that shows **all screenshots** from a given run.

- After uploading the per-scenario screenshots/logs, generate an `index.html` that embeds the screenshot URLs (and links to logs).
- After uploading the per-scenario screenshots/logs/reports, generate an `index.html` that embeds the screenshot URLs (and links to logs + `*-report.json`).
- Upload that `index.html` via `wtf-upload` as well (it’s in `$PATH`) and share the resulting single gallery URL in review/CI output.

Convenience wrapper (uploads smoke artifacts + generates/uploads `index.html`):

```bash
cd ~/dev/krita/krita-docker-setup
./bin/krita-smoke-upload-gallery
```

Example URLs (2026‑01‑17):

- Android `top-bar` (desktop chrome hidden): https://tmp.uh-oh.wtf/2026/01/17/0f875f21-smoke-android-krita-top-bar.png
- Android `copypaste`: https://tmp.uh-oh.wtf/2026/01/17/7537d34e-smoke-android-krita-copypaste.png
- Android `copypaste` logcat: https://tmp.uh-oh.wtf/2026/01/17/0fdceea8-smoke-android-krita-copypaste-logcat.txt
- Android `quickmenu`: https://tmp.uh-oh.wtf/2026/01/17/724db102-smoke-android-krita-quickmenu.png
- Linux `top-bar`: https://tmp.uh-oh.wtf/2026/01/17/dcaf1ec1-smoke-linux-krita-top-bar.png
- Linux `top-bar` log: https://tmp.uh-oh.wtf/2026/01/17/6477b1dc-smoke-linux-krita-top-bar-log.txt

More recent URLs (2026‑01‑17, after icon + dock-chrome updates):

- Android `top-bar`: https://tmp.uh-oh.wtf/2026/01/17/64f5b45a-smoke-android-krita-top-bar.png
- Android `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/17/275539e9-smoke-android-krita-touch-sidebar.png
- Android `copypaste`: https://tmp.uh-oh.wtf/2026/01/17/7537d34e-smoke-android-krita-copypaste.png
- Android `copypaste` logcat: https://tmp.uh-oh.wtf/2026/01/17/0fdceea8-smoke-android-krita-copypaste-logcat.txt
- Linux `top-bar`: https://tmp.uh-oh.wtf/2026/01/17/4c02cbbc-smoke-linux-krita-top-bar.png
- Linux `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/17/d0371a13-smoke-linux-krita-touch-sidebar.png

Prior URLs (2026‑01‑18, after hiding the MDI tab/title strip in Touch Mode + forcing QMdiArea scrollbars off):

Android:

- `top-bar`: https://tmp.uh-oh.wtf/2026/01/18/356290ec-smoke-android-krita-top-bar.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/6463d39b-smoke-android-krita-top-bar-logcat.txt)
- `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/18/910103b5-smoke-android-krita-touch-sidebar.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/7fa8aa21-smoke-android-krita-touch-sidebar-logcat.txt)
- `selection-tool`: https://tmp.uh-oh.wtf/2026/01/18/cab1c2f9-smoke-android-krita-selection-tool.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/e56424a4-smoke-android-krita-selection-tool-logcat.txt)
- `transform-tool`: https://tmp.uh-oh.wtf/2026/01/18/bcb8e2ff-smoke-android-krita-transform-tool.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/66ab9e7d-smoke-android-krita-transform-tool-logcat.txt)
- `layers-panel`: https://tmp.uh-oh.wtf/2026/01/18/732d389f-smoke-android-krita-layers-panel.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/000e97f6-smoke-android-krita-layers-panel-logcat.txt)
- `layer-options`: https://tmp.uh-oh.wtf/2026/01/18/48ec1d06-smoke-android-krita-layer-options.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/df54642d-smoke-android-krita-layer-options-logcat.txt)
- `color-panel`: https://tmp.uh-oh.wtf/2026/01/18/93935dc6-smoke-android-krita-color-panel.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/3633a811-smoke-android-krita-color-panel-logcat.txt)
- `colordrop`: https://tmp.uh-oh.wtf/2026/01/18/bc3c49a9-smoke-android-krita-colordrop.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/39810fc9-smoke-android-krita-colordrop-logcat.txt)
- `quickshape`: https://tmp.uh-oh.wtf/2026/01/18/f2759e96-smoke-android-krita-quickshape.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/df2a966f-smoke-android-krita-quickshape-logcat.txt)
- `actions-sheet`: https://tmp.uh-oh.wtf/2026/01/18/88e70da8-smoke-android-krita-actions-sheet.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/041a9f88-smoke-android-krita-actions-sheet-logcat.txt)
- `gesture-controls`: https://tmp.uh-oh.wtf/2026/01/18/b7451b1e-smoke-android-krita-gesture-controls.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/87f46633-smoke-android-krita-gesture-controls-logcat.txt)
- `quickmenu`: https://tmp.uh-oh.wtf/2026/01/18/cfe3802e-smoke-android-krita-quickmenu.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/f9111078-smoke-android-krita-quickmenu-logcat.txt)
- `quickmenu-setup`: https://tmp.uh-oh.wtf/2026/01/18/f4a530b3-smoke-android-krita-quickmenu-setup.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/4097630c-smoke-android-krita-quickmenu-setup-logcat.txt)
- `copypaste`: https://tmp.uh-oh.wtf/2026/01/18/29e2b402-smoke-android-krita-copypaste.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/8edf1195-smoke-android-krita-copypaste-logcat.txt)

Linux:

- `top-bar`: https://tmp.uh-oh.wtf/2026/01/18/a327f376-smoke-linux-krita-top-bar.png (log: https://tmp.uh-oh.wtf/2026/01/18/9ee2b825-smoke-linux-krita-top-bar-log.txt)
- `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/18/89874fde-smoke-linux-krita-touch-sidebar.png (log: https://tmp.uh-oh.wtf/2026/01/18/f58f2500-smoke-linux-krita-touch-sidebar-log.txt)
- `selection-tool`: https://tmp.uh-oh.wtf/2026/01/18/f3532f40-smoke-linux-krita-selection-tool.png (log: https://tmp.uh-oh.wtf/2026/01/18/f54aa2df-smoke-linux-krita-selection-tool-log.txt)
- `transform-tool`: https://tmp.uh-oh.wtf/2026/01/18/6117b44e-smoke-linux-krita-transform-tool.png (log: https://tmp.uh-oh.wtf/2026/01/18/c9dff9b4-smoke-linux-krita-transform-tool-log.txt)
- `layers-panel`: https://tmp.uh-oh.wtf/2026/01/18/feb0b9db-smoke-linux-krita-layers-panel.png (log: https://tmp.uh-oh.wtf/2026/01/18/dc716f8d-smoke-linux-krita-layers-panel-log.txt)
- `layer-options`: https://tmp.uh-oh.wtf/2026/01/18/25082a55-smoke-linux-krita-layer-options.png (log: https://tmp.uh-oh.wtf/2026/01/18/54e3ae04-smoke-linux-krita-layer-options-log.txt)
- `color-panel`: https://tmp.uh-oh.wtf/2026/01/18/1e44db77-smoke-linux-krita-color-panel.png (log: https://tmp.uh-oh.wtf/2026/01/18/21d931d3-smoke-linux-krita-color-panel-log.txt)
- `colordrop`: https://tmp.uh-oh.wtf/2026/01/18/82fc3737-smoke-linux-krita-colordrop.png (log: https://tmp.uh-oh.wtf/2026/01/18/214eec39-smoke-linux-krita-colordrop-log.txt)
- `quickshape`: https://tmp.uh-oh.wtf/2026/01/18/df5e36b4-smoke-linux-krita-quickshape.png (log: https://tmp.uh-oh.wtf/2026/01/18/447f2de2-smoke-linux-krita-quickshape-log.txt)
- `actions-sheet`: https://tmp.uh-oh.wtf/2026/01/18/5ff39d26-smoke-linux-krita-actions-sheet.png (log: https://tmp.uh-oh.wtf/2026/01/18/8853192f-smoke-linux-krita-actions-sheet-log.txt)
- `gesture-controls`: https://tmp.uh-oh.wtf/2026/01/18/345230d2-smoke-linux-krita-gesture-controls.png (log: https://tmp.uh-oh.wtf/2026/01/18/2b99c2bf-smoke-linux-krita-gesture-controls-log.txt)
- `quickmenu`: https://tmp.uh-oh.wtf/2026/01/18/3b84bc5b-smoke-linux-krita-quickmenu.png (log: https://tmp.uh-oh.wtf/2026/01/18/732fbb6e-smoke-linux-krita-quickmenu-log.txt)
- `quickmenu-setup`: https://tmp.uh-oh.wtf/2026/01/18/a5201549-smoke-linux-krita-quickmenu-setup.png (log: https://tmp.uh-oh.wtf/2026/01/18/90e5270f-smoke-linux-krita-quickmenu-setup-log.txt)
- `copypaste`: https://tmp.uh-oh.wtf/2026/01/18/d77e0400-smoke-linux-krita-copypaste.png (log: https://tmp.uh-oh.wtf/2026/01/18/571f0dd2-smoke-linux-krita-copypaste-log.txt)

Latest URLs (2026‑01‑18, full Linux + Android smoke suites pass):

Android:

- `top-bar`: https://tmp.uh-oh.wtf/2026/01/18/80765e42-smoke-android-krita-top-bar.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/4b2effc1-smoke-android-krita-top-bar-logcat.txt)
- `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/18/30d8b26a-smoke-android-krita-touch-sidebar.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/7237bea0-smoke-android-krita-touch-sidebar-logcat.txt)
- `selection-tool`: https://tmp.uh-oh.wtf/2026/01/18/6dcc53e8-smoke-android-krita-selection-tool.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/57225617-smoke-android-krita-selection-tool-logcat.txt)
- `transform-tool`: https://tmp.uh-oh.wtf/2026/01/18/e2819bb7-smoke-android-krita-transform-tool.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/00881eb6-smoke-android-krita-transform-tool-logcat.txt)
- `layers-panel`: https://tmp.uh-oh.wtf/2026/01/18/8f530d27-smoke-android-krita-layers-panel.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/5858234b-smoke-android-krita-layers-panel-logcat.txt)
- `layer-options`: https://tmp.uh-oh.wtf/2026/01/18/ed4930d3-smoke-android-krita-layer-options.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/14747f5c-smoke-android-krita-layer-options-logcat.txt)
- `color-panel`: https://tmp.uh-oh.wtf/2026/01/18/9ad0678e-smoke-android-krita-color-panel.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/278d0f64-smoke-android-krita-color-panel-logcat.txt)
- `colordrop`: https://tmp.uh-oh.wtf/2026/01/18/6fd1011e-smoke-android-krita-colordrop.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/d4923dda-smoke-android-krita-colordrop-logcat.txt)
- `quickshape`: https://tmp.uh-oh.wtf/2026/01/18/b9165516-smoke-android-krita-quickshape.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/a4ec2890-smoke-android-krita-quickshape-logcat.txt)
- `actions-sheet`: https://tmp.uh-oh.wtf/2026/01/18/1ed1441a-smoke-android-krita-actions-sheet.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/208a0b42-smoke-android-krita-actions-sheet-logcat.txt)
- `gesture-controls`: https://tmp.uh-oh.wtf/2026/01/18/6fd70e51-smoke-android-krita-gesture-controls.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/35009b87-smoke-android-krita-gesture-controls-logcat.txt)
- `quickmenu`: https://tmp.uh-oh.wtf/2026/01/18/a444638b-smoke-android-krita-quickmenu.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/2fdbaf7c-smoke-android-krita-quickmenu-logcat.txt)
- `quickmenu-setup`: https://tmp.uh-oh.wtf/2026/01/18/614430f9-smoke-android-krita-quickmenu-setup.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/136473c3-smoke-android-krita-quickmenu-setup-logcat.txt)
- `copypaste`: https://tmp.uh-oh.wtf/2026/01/18/7d96340d-smoke-android-krita-copypaste.png (logcat: https://tmp.uh-oh.wtf/2026/01/18/5b4fa174-smoke-android-krita-copypaste-logcat.txt)

Linux:

- `top-bar`: https://tmp.uh-oh.wtf/2026/01/18/137bf112-smoke-linux-krita-top-bar.png (log: https://tmp.uh-oh.wtf/2026/01/18/6e7d03da-smoke-linux-krita-top-bar-log.txt)
- `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/18/df48b3df-smoke-linux-krita-touch-sidebar.png (log: https://tmp.uh-oh.wtf/2026/01/18/a73ebc5c-smoke-linux-krita-touch-sidebar-log.txt)
- `selection-tool`: https://tmp.uh-oh.wtf/2026/01/18/e7d2c310-smoke-linux-krita-selection-tool.png (log: https://tmp.uh-oh.wtf/2026/01/18/3a524f42-smoke-linux-krita-selection-tool-log.txt)
- `transform-tool`: https://tmp.uh-oh.wtf/2026/01/18/2f876994-smoke-linux-krita-transform-tool.png (log: https://tmp.uh-oh.wtf/2026/01/18/58902ad6-smoke-linux-krita-transform-tool-log.txt)
- `layers-panel`: https://tmp.uh-oh.wtf/2026/01/18/2e44f94b-smoke-linux-krita-layers-panel.png (log: https://tmp.uh-oh.wtf/2026/01/18/346d2170-smoke-linux-krita-layers-panel-log.txt)
- `layer-options`: https://tmp.uh-oh.wtf/2026/01/18/6aa43f03-smoke-linux-krita-layer-options.png (log: https://tmp.uh-oh.wtf/2026/01/18/71352711-smoke-linux-krita-layer-options-log.txt)
- `color-panel`: https://tmp.uh-oh.wtf/2026/01/18/92f6ef52-smoke-linux-krita-color-panel.png (log: https://tmp.uh-oh.wtf/2026/01/18/de9d5b32-smoke-linux-krita-color-panel-log.txt)
- `colordrop`: https://tmp.uh-oh.wtf/2026/01/18/a7b21bf9-smoke-linux-krita-colordrop.png (log: https://tmp.uh-oh.wtf/2026/01/18/7d2c5226-smoke-linux-krita-colordrop-log.txt)
- `quickshape`: https://tmp.uh-oh.wtf/2026/01/18/5df3370d-smoke-linux-krita-quickshape.png (log: https://tmp.uh-oh.wtf/2026/01/18/8ec2bf56-smoke-linux-krita-quickshape-log.txt)
- `actions-sheet`: https://tmp.uh-oh.wtf/2026/01/18/a4e49d97-smoke-linux-krita-actions-sheet.png (log: https://tmp.uh-oh.wtf/2026/01/18/f1c0d7e6-smoke-linux-krita-actions-sheet-log.txt)
- `gesture-controls`: https://tmp.uh-oh.wtf/2026/01/18/a518143f-smoke-linux-krita-gesture-controls.png (log: https://tmp.uh-oh.wtf/2026/01/18/c9555ea0-smoke-linux-krita-gesture-controls-log.txt)
- `quickmenu`: https://tmp.uh-oh.wtf/2026/01/18/e63ce45d-smoke-linux-krita-quickmenu.png (log: https://tmp.uh-oh.wtf/2026/01/18/f936f6e3-smoke-linux-krita-quickmenu-log.txt)
- `quickmenu-setup`: https://tmp.uh-oh.wtf/2026/01/18/bd123ce5-smoke-linux-krita-quickmenu-setup.png (log: https://tmp.uh-oh.wtf/2026/01/18/a51713d1-smoke-linux-krita-quickmenu-setup-log.txt)
- `copypaste`: https://tmp.uh-oh.wtf/2026/01/18/e169754d-smoke-linux-krita-copypaste.png (log: https://tmp.uh-oh.wtf/2026/01/18/87431ea6-smoke-linux-krita-copypaste-log.txt)

Linux AppImage (2026‑01‑19):

- `top-bar`: https://tmp.uh-oh.wtf/2026/01/19/38207d63-smoke-linux-appimage-top-bar.png (log: https://tmp.uh-oh.wtf/2026/01/19/765e18b7-smoke-linux-appimage-top-bar-log.txt)
- `touch-sidebar`: https://tmp.uh-oh.wtf/2026/01/19/9ea17176-smoke-linux-appimage-touch-sidebar.png (log: https://tmp.uh-oh.wtf/2026/01/19/0cd6cd9f-smoke-linux-appimage-touch-sidebar-log.txt)
- `selection-tool`: https://tmp.uh-oh.wtf/2026/01/19/aca4492a-smoke-linux-appimage-selection-tool.png (log: https://tmp.uh-oh.wtf/2026/01/19/df9e8d60-smoke-linux-appimage-selection-tool-log.txt)
- `transform-tool`: https://tmp.uh-oh.wtf/2026/01/19/5e73b1be-smoke-linux-appimage-transform-tool.png (log: https://tmp.uh-oh.wtf/2026/01/19/552fc46b-smoke-linux-appimage-transform-tool-log.txt)
- `layers-panel`: https://tmp.uh-oh.wtf/2026/01/19/efa23c04-smoke-linux-appimage-layers-panel.png (log: https://tmp.uh-oh.wtf/2026/01/19/c77f0afc-smoke-linux-appimage-layers-panel-log.txt)
- `layer-options`: https://tmp.uh-oh.wtf/2026/01/19/e9d69b77-smoke-linux-appimage-layer-options.png (log: https://tmp.uh-oh.wtf/2026/01/19/0428bb42-smoke-linux-appimage-layer-options-log.txt)
- `color-panel`: https://tmp.uh-oh.wtf/2026/01/19/78683ddd-smoke-linux-appimage-color-panel.png (log: https://tmp.uh-oh.wtf/2026/01/19/79ee2abb-smoke-linux-appimage-color-panel-log.txt)
- `colordrop`: https://tmp.uh-oh.wtf/2026/01/19/7fcd2293-smoke-linux-appimage-colordrop.png (log: https://tmp.uh-oh.wtf/2026/01/19/4d7d286d-smoke-linux-appimage-colordrop-log.txt)
- `quickshape`: https://tmp.uh-oh.wtf/2026/01/19/79ca03ee-smoke-linux-appimage-quickshape.png (log: https://tmp.uh-oh.wtf/2026/01/19/7604d206-smoke-linux-appimage-quickshape-log.txt)
- `actions-sheet`: https://tmp.uh-oh.wtf/2026/01/19/cfffe1ab-smoke-linux-appimage-actions-sheet.png (log: https://tmp.uh-oh.wtf/2026/01/19/fd10a082-smoke-linux-appimage-actions-sheet-log.txt)
- `gesture-controls`: https://tmp.uh-oh.wtf/2026/01/19/bb420c73-smoke-linux-appimage-gesture-controls.png (log: https://tmp.uh-oh.wtf/2026/01/19/0487f42c-smoke-linux-appimage-gesture-controls-log.txt)
- `quickmenu`: https://tmp.uh-oh.wtf/2026/01/19/395e8650-smoke-linux-appimage-quickmenu.png (log: https://tmp.uh-oh.wtf/2026/01/19/ae561d9b-smoke-linux-appimage-quickmenu-log.txt)
- `quickmenu-setup`: https://tmp.uh-oh.wtf/2026/01/19/1ea238bd-smoke-linux-appimage-quickmenu-setup.png (log: https://tmp.uh-oh.wtf/2026/01/19/ed969dc3-smoke-linux-appimage-quickmenu-setup-log.txt)
- `copypaste`: https://tmp.uh-oh.wtf/2026/01/19/7760cbbd-smoke-linux-appimage-copypaste.png (log: https://tmp.uh-oh.wtf/2026/01/19/8880df12-smoke-linux-appimage-copypaste-log.txt)

### 4.4 Android smoke commands (Genymotion Cloud)

We use Genymotion Cloud (SaaS) because we typically lack KVM on AWS.

Canonical CI recipe:

- Name: `krita_tablet_nexus10_api35`
- Recipe UUID: `a524e263-97a2-427e-aa81-a739dc8dba2a`
- OS image UUID: `a7025999-0630-471d-804f-81966968f080` (Android 15 / API 35 / arm64)
- HW profile UUID: `c6e1222c-993e-4bf2-840f-4795792f2143` (Google Nexus 10 2560×1600 / arm64)

Implication:

- Prefer `arm64-v8a` APKs for Genymotion Cloud.

Convenience scripts:

- `./bin/krita-gmsaas-smoke-touch` (recommended batch runner for touch scenarios)
- `./bin/gmsaas-smoke-android` (lower-level runner; use for debugging)

```bash
cd ~/dev/krita/krita-docker-setup
source ~/.api-keys   # exports GENYMOTION_API_KEY
./bin/krita-gmsaas-smoke-touch
```

Notes:

- The script **wakes/unlocks** the device before launching, otherwise screenshots may capture the **lock screen**.
- For `--scenario` runs, the script waits for a logcat marker printed by Krita:
  - `KRITA_TOUCH_SMOKE_DONE scenario=<name> status=<OK|ERROR>`
  - `--wait` is the **timeout** for this marker (fallback: sleep if `timeout(1)` is missing).
  - Use `--no-marker-wait` to force the legacy “sleep then screenshot” behavior.
- The script also saves a machine-readable report alongside the screenshot + logcat:
  - First it pulls `files/touch-smoke-report.json` from the app sandbox via `adb exec-out run-as ...` (avoids logcat truncation).
  - Fallback: extracts `KRITA_TOUCH_SMOKE_JSON ...` into `smoke-android-*-report.json`.
- The script best-effort dismisses Android’s first-run “Viewing full screen” / “Got it” system tip (if detected via `uiautomator dump`) so screenshots show Krita.
- The script uses bounded `timeout(1)` for ADB operations (install, launch, screencap, logcat) to avoid CI hangs.
- The script also uses timeouts for Genymotion provisioning + `adbconnect` so instance startup can’t hang for hours.
- If the ADB tunnel flakes mid-run and you see `device 'localhost:<port>' not found`, the script will re-`adbconnect` and retry screenshot/logcat once.
- Genymotion Cloud can occasionally fail to start an instance (instance state becomes `ERROR`).
  - `./bin/gmsaas-smoke-android` retries instance start a few times and best-effort stops `ERROR` instances so CI doesn’t leak devices.
  - If you still end up with a stuck `ERROR` instance, cleanup + retry:

  ```bash
  source ~/.api-keys
  gmsaas instances list
  gmsaas instances stop <UUID>
  gmsaas adb stop
  ```
- If you see `TOO_MANY_RUNNING_VDS`, a previous run probably leaked a Genymotion instance (often due to CI timeout / aborted run). Cleanup:

  ```bash
  source ~/.api-keys
  gmsaas instances list
  gmsaas instances stop <UUID>
  gmsaas adb stop
  ```

  CI mitigation: keep `--max-run` small (example: `--max-run 20`) so leaked devices auto-expire sooner.

Scenario screenshot:

```bash
cd ~/dev/krita/krita-docker-setup
source ~/.api-keys   # exports GENYMOTION_API_KEY
./bin/krita-gmsaas-smoke-touch --scenario selection-tool
```

Multiple scenarios in one provisioned instance (faster/cheaper CI):

```bash
cd ~/dev/krita/krita-docker-setup
source ~/.api-keys   # exports GENYMOTION_API_KEY
./bin/krita-gmsaas-smoke-touch \
  --scenario selection-tool \
  --scenario quickmenu \
  --scenario copypaste \
  --scenario layer-options \
  --scenario actions-sheet \
  --scenario top-bar \
  --scenario color-panel \
  --scenario colordrop
```

Pass/fail (minimum):

- install succeeds
- app launches
- screenshot is non-empty
- no `FATAL EXCEPTION` attributed to the app in logcat during the run

### 4.5 Touch smoke test architecture (needed)

We currently use `--touch-smoke` for deterministic screenshots and *some* behavioral assertions, but it does **not** cover “almost all features” of Krita. The value is specifically in catching regressions in our **touch-first Procreate-MVP** surfaces, across **Linux Xvfb** and **Android/Genymotion**.

The need:

- Screenshot-only smoke is helpful, but it misses “looks OK, behaves wrong” regressions (gesture routing, selection semantics, tool state restoration, config gating).
- Android input synthesis can be flaky; tests must be deterministic even when Qt synthetic events fail.
- We need confidence that the same scenarios behave the same on Linux and on the Genymotion recipe.

Architecture direction (v2):

- **Reusable primitives** in C++ (not copy/paste per scenario):
  - open/ensure doc; show/hide dockers by id; switch tool; trigger action by id
  - set canvas resources (fg/bg color); create layers; query layer count; query selection rect
  - paint/fill via input events **with fallbacks** to direct paint ops
  - simulate touch-ish interactions where needed (tap, swipe, hold) using `QMouseEvent` with synthesized sources
- **Assertions first**:
  - validate tool state (active tool id), selection presence/shape, and at least a few pixel samples (inside/outside)
  - validate “restore” behaviors (e.g. Modify returns to previous tool and restores `touchPainting`)
  - validate config gating (“Gesture Controls” toggles actually change behavior)
- **Better reporting**:
  - keep the existing log marker `KRITA_TOUCH_SMOKE_DONE scenario=<name> status=<OK|ERROR>`
  - emit a machine-readable report containing per-step pass/fail + details, so scripts can give useful CI diffs without image comparison:
    - keep `KRITA_TOUCH_SMOKE_JSON { ... }` (easy to grep on Linux)
    - on Android also write the full report to `touch-smoke-report.json` in the app’s files dir so runners can pull it reliably (logcat can truncate long lines)
    - Example shape:
      - `KRITA_TOUCH_SMOKE_JSON {"scenario":"layers-panel","status":"OK","duration_ms":1234,"steps":[{"name":"layers_panel.find_node_view","ok":true},{"name":"layers_panel.hold_visibility_solo","ok":true,"details":{"before_visible":7,"after_visible":1}}]}`

Next test targets (cross-platform, assertive):

- QuickMenu: now covered by `--touch-smoke=quickmenu` (gesture-path slide/highlight, release triggers slot action, hold opens config sheet) and `--touch-smoke=gesture-controls` (QuickMenu enable/disable gating).
- Copy/Paste overlay: now covered by `--touch-smoke=copypaste` (selection → copy to new layer + pixel assertions for copied content + outside-selection transparency) and `--touch-smoke=gesture-controls` (3-finger swipe down gating opens the overlay).
- Gesture Controls: now covered by `--touch-smoke=gesture-controls` (rotate-with-pinch + clipboard + undo/redo + quickmenu + clear-layer scrub + fullscreen gating assertions), **including input-manager-path checks** by driving rotate-with-pinch, the 3-finger clipboard swipe, the 2/3-finger undo/redo taps, **and** the 3-finger clear-layer scrub via synthetic touch events delivered to the canvas (exercises `KisInputManager` shortcut matching; not just direct input-action calls).
- Layers: now covered by `--touch-smoke=layers-panel` (swipe-right multi-select + hold-visibility solo/restore) and `--touch-smoke=layer-options` (swipe-left opens options).

Next offer (recommended next implementation):

- Extend the **scriptable touch-smoke runner** so we can author more elaborate, cross-platform tests without growing `KisApplication.cpp` endlessly:
  - Add primitives: `drag`, `pinch_rotate`, `waitFor(pixel|overlayVisible)`, etc. (Qt touch injection first, direct-action fallback when injection is flaky).
  - Grow a small suite of JSON scripts for our Procreate-MVP surfaces, and run them on both Linux Xvfb and Android/Genymotion.

---

## 5) Procreate Reference Index (Docs)

These pages define the behaviors we emulate:

- Interface overview: https://help.procreate.com/procreate/handbook/interface-gestures/interface
- Gestures: https://help.procreate.com/procreate/handbook/interface-gestures/gestures
- Actions → Preferences (includes “Gesture Controls”): https://help.procreate.com/procreate/handbook/actions/actions-preferences
- QuickMenu: https://help.procreate.com/procreate/handbook/interface-gestures/quickmenu
- Copy/Paste menu: https://help.procreate.com/procreate/handbook/interface-gestures/copypaste
- Selections landing: https://help.procreate.com/procreate/handbook/selections
- Selections interface: https://help.procreate.com/procreate/handbook/selections/selections-interface
- Automatic selection: https://help.procreate.com/procreate/handbook/selections/selections-automatic
- Freehand selection: https://help.procreate.com/procreate/handbook/selections/selections-freehand
- Transform gestures: https://help.procreate.com/procreate/handbook/transform/transform-interface-gestures
- Layers interface: https://help.procreate.com/procreate/handbook/layers/layers-interface
- Colors interface: https://help.procreate.com/procreate/handbook/colors/colors-interface

---

## 6) Procreate Interaction Inventory → Krita Parity Map

This section describes Procreate behaviors in words, then how we aim to reproduce the “feel” in Krita.

### 6.0 Procreate’s “Gesture Controls” model (important for touch-first UX)

Procreate exposes a single place to configure touch/gesture behavior (Actions → Prefs → Gesture Controls). Key ideas we should emulate:

- Gestures are **first-class, configurable shortcuts**:
  - you can enable/disable features
  - some shortcuts have a configurable **touch-and-hold delay**
  - you can assign multiple shortcuts to the same feature
  - but you cannot bind the same shortcut to two different features (conflict warning)
- **Disable Touch actions**: makes finger input “gesture shortcuts only” (no finger painting).
- **Canvas gestures**:
  - pinch to zoom; pinch-twist to rotate (toggleable via “Rotate with pinch zoom”)
  - two-finger drag to pan
  - quick pinch to fit canvas to screen
- **Undo/Redo gestures**:
  - two-finger tap = undo; three-finger tap = redo
  - tap-and-hold repeats undo/redo (with a configurable delay)
- **Clipboard gestures**:
  - three-finger swipe down opens Copy/Paste menu
- **System/visibility gestures**:
  - four-finger tap toggles full screen UI
- **Advanced gestures**:
  - QuickShape (draw & hold; second-finger tap to “perfect” shapes)
  - Eyedropper (tap/hold, Modify button)
  - Layer Select (touch gesture to pick layer contents under finger)
- **Accessibility**:
  - Single Touch Gestures Companion (on-screen buttons that replace multi-finger gestures)

Krita parity plan:

- Treat **Touch Mode** as the “bundle” that enables this set of choices by default.
- Implement/ship one default profile (**Touch Gestures Only**) that matches Procreate defaults.
- Because we ship **finger = gestures-only** by default:
  - map **one-finger drag** to pan (no finger painting to conflict)
  - map **two-finger drag** to zoom/rotate
- Provide a simplified Touch Mode preferences page (“Gesture Controls”-like) that toggles:
  - touch painting off/on/auto (`touchPainting`)
  - rotate-with-pinch enable/disable (`touchRotateWithPinchEnabled`)
  - quick-pinch-to-fit enable/disable (`touchQuickPinchToFitEnabled`)
  - QuickShape enable/disable (`touchQuickShapeEnabled`)
  - QuickMenu gesture enable/disable (`touchQuickMenuEnabled`)
  - undo/redo gesture enable/disable (`touchUndoRedoGesturesEnabled`)
  - clipboard gesture enable/disable (`touchClipboardGestureEnabled`)
  - canvas-only (fullscreen-like) gesture enable/disable (`touchFullscreenGestureEnabled`)
  - *(hold-repeat delay is deferred; Undo/Redo repeat exists via the Touch sidebar buttons)*

Code checkpoints (Krita touch/gesture system):

- Touch painting gate:
  - `libs/ui/kis_config.{h,cc}` (`touchPainting`, `disableTouchOnCanvas()`)
  - `libs/ui/tool/kis_tool_freehand.cc` (touch masking behavior)
- Touch gestures → actions:
  - `libs/ui/input/kis_touch_shortcut.{h,cpp}` (1–5 finger tap/drag/hold matching)
  - `libs/ui/input/kis_shortcut_matcher.cpp` (selection of touch shortcuts)
  - `libs/ui/input/kis_input_manager_p.cpp` (`addTouchShortcut`)
  - `krita/data/input/touch-gestures-only.profile` (default mapping we ship)
  - gesture gating for the toggles above:
    - `libs/ui/input/kis_zoom_and_rotate_action.cpp` (rotate-with-pinch + quick pinch fit)
    - `libs/ui/input/KisTouchQuickMenuAction.cpp` (QuickMenu)
    - `libs/ui/input/KisTouchGestureAction.cpp` (Undo/Redo, clipboard, canvas-only)
  - toggle UI surface:
    - touch Actions sheet (`libs/ui/widgets/kis_touch_actions_sheet.{h,cpp}`) → **Gestures** page
    - action wiring (`libs/ui/KisMainWindow.cpp`) adds the checkable actions:
      - `touch_rotate_with_pinch`
      - `touch_quick_pinch_to_fit_enabled`
      - `touch_quickshape_enabled`
      - `touch_quickmenu_enabled`
      - `touch_clipboard_gesture_enabled`
      - `touch_undo_redo_gestures_enabled`
      - `touch_fullscreen_gesture_enabled`
      - `touch_painting_auto` / `touch_painting_enabled` / `touch_painting_disabled`

Manual acceptance checklist:

- Finger never paints by accident (unless explicitly enabled).
- Undo/redo gestures match Procreate defaults and are reliable.
- Clipboard + fullscreen gestures do not conflict with OS-level gestures (Android especially).
- A single “Touch Mode” toggle makes the experience predictable.

### 6.1 Navigation + core gestures (P0)

Procreate-like mental model:

- Canvas navigation:
  - pinch to zoom
  - two-finger drag to pan (because finger normally draws in Procreate)
  - pinch-twist to rotate (optional toggle; must be consistent)
  - quick pinch to fit-to-screen
- High-frequency commands:
  - two-finger tap = undo; three-finger tap = redo
  - tap-and-hold repeats undo/redo
  - four-finger tap toggles full-screen UI
  - three-finger swipe down opens Copy/Paste menu
- Drawing helper gesture:
  - QuickShape: draw & hold to snap; second-finger tap to perfect the shape

Krita parity plan:

- Encode touch gestures using Krita input profiles + touch shortcut matcher.
- Default profile for Touch Mode: **Touch Gestures Only** (`krita/data/input/touch-gestures-only.profile`).
- With touch painting disabled (our default), navigation is:
  - **one-finger drag** = pan
  - **two-finger drag** = zoom/rotate
- Quick Pinch to Fit:
  - In Touch Mode, a “quick pinch” triggers `toggle_zoom_to_fit` (fit-to-screen, then repeat to restore the prior view).
  - Controlled by `touchQuickPinchToFitEnabled` (Actions sheet → Gestures).
- Validate routing on real X11 touchscreen hardware:
  - When a tool is “modal” (Transform, Selection), gestures must route to the tool when appropriate.
  - Otherwise gestures route to the canvas (pan/zoom/rotate).

Code checkpoints:

- Touch profile assets:
  - `krita/data/input/touch-gestures-only.profile`
  - `krita/data/input/CMakeLists.txt`
- Core view-transform actions (touch handling lives here):
  - `libs/ui/input/kis_pan_action.cpp`
  - `libs/ui/input/kis_zoom_action.cpp`
  - `libs/ui/input/kis_rotate_canvas_action.cpp`
  - `libs/ui/input/kis_input_manager.cpp` (event filter + touch state machine)

Manual acceptance checklist:

- Finger never paints by accident.
- Pinch/pan/rotate are reliable.
- Gestures do not fight transform mode when active (see transform playbook).

### 6.2 Procreate-style top chrome + touch sidebar (P1)

Procreate-like mental model:

- Top bar has two clusters:
  - Left: Actions, Adjustments, Selection, Transform (and Home/Gallery)
  - Right: Brush, Smudge, Eraser, Layers, Color
- Sidebar (left or right, depending on handedness):
  - Brush size
  - Opacity
  - Undo/redo
  - Modify (eyedropper/modifier)
- Sidebar affordances:
  - “fine control” while holding slider (drag away from the slider for precision)
  - Modify supports temporary eyedropper (hold Modify, tap/circle to pick colors)
  - Undo/Redo can be “rapid” (tap-and-hold repeats)

Krita parity plan:

- Use a dedicated **Touch Procreate-like workspace**.
- Evolve Krita’s existing Touch Docker into a Procreate-like sidebar (done for v1).
- Rebuild the top chrome as either:
  - a dedicated touch toolbar layout (workspace-managed), or
  - a touch overlay (“top bar”) that is independent of Qt toolbars.

Acceptance checklist:

- All core actions are reachable with one tap.
- Sliders are usable with fingers.
- Sidebar works in canvas-only/fullscreen.

### 6.3 Selection tool model (P0)

Procreate-like mental model:

- One selection tool with mode buttons:
  - Automatic (threshold)
  - Freehand (lasso + tap-to-polygon)
  - Rectangle
  - Ellipse
- Common operations:
  - Add / Remove / Intersect
  - Invert
  - Feather
  - Save & Load selection
  - Color Fill / Clear (selection operations)

Krita parity plan:

- Implement `KisToolSelectTouch` as the default touch selection entry point.
- Keep it pixel-selection-only for touch simplicity.

### 6.4 Transform model (P0)

Procreate-like mental model:

- Transform is a dedicated mode.
- While active, pinch/rotate manipulates transform, not canvas.

Krita parity plan:

- Ensure transform mode changes gesture routing.
- Provide a clear exit.

### 6.5 Clipboard + editing gestures (P1)

Procreate-like mental model:

- Three-finger swipe down opens a small **Copy/Paste** overlay.
- Menu options include:
  - Cut, Copy, Copy All, Paste
  - Duplicate (selection/content duplicate)
  - Cut & Paste (move selection into a new layer)
- Three-finger scrub clears the current layer (quick erase).

Krita parity plan:

- Bind the gesture to a Krita touch action that opens a touch overlay.
- Prefer mapping to existing selection manager actions:
  - `edit_cut`, `edit_copy`, `edit_paste`, `copy_merged`
  - `copy_selection_to_new_layer`, `cut_selection_to_new_layer`
- Three-finger scrub (horizontal back-and-forth) maps to Krita’s `clear` action (“Clear Layer” feel).
  - Routed via the same **3-finger drag** gesture as Copy/Paste, but direction-classified:
    - swipe down → Copy/Paste overlay
    - scrub sideways → clear layer
  - gated by `touchClearLayerGestureEnabled` in the Actions sheet (Gestures page)

### 6.6 Layers panel + layer gestures (P1)

Procreate-like mental model:

- Layers button opens a compact layers panel.
- Tap layer selects it; tap again opens layer options menu.
- Visibility has a “solo” gesture (press-and-hold visibility toggles others off).
- Touch gestures inside the layer list:
  - swipe left reveals Lock / Duplicate / Delete
  - swipe right selects multiple layers
  - pinch to merge layers
  - two-finger swipe right on a layer = Alpha Lock
  - two-finger tap on a layer = Opacity control
  - two-finger hold on a layer = Select layer contents

Krita parity plan:

- Keep using `KisLayerBox` as the underlying layer model, but add a touch layer:
  - touch-friendly row height + hit targets
  - swipe gestures for common commands
  - a touch-first layer options sheet (instead of right-click context menus)
- Map gestures to existing actions where possible (duplicate layer, delete layer, alpha lock, layer properties).

### 6.7 Colors + eyedropper + ColorDrop (P1/P2)

Procreate-like mental model:

- Color button opens a color panel with:
  - active color + previous color swap
  - multiple pickers (disc/classic/harmony/value) and palettes
- Eyedropper:
  - tap/hold on canvas to pick
  - Modify button acts as a fast temporary eyedropper
- ColorDrop:
  - drag active color onto canvas to fill (contiguous) with adjustable threshold
  - “Continue filling” style flow to fill multiple regions quickly
- Recolor:
  - pick a target color region and replace with current color (threshold controlled)

Krita parity plan:

- v1: rely on existing `KisToolColorSampler` (Modify button + tool) + existing color selector docker.
- v2: implement a Procreate-like temporary eyedropper overlay (no tool switch, returns to prior tool).
- ColorDrop parity (v1):
  - drag the foreground color (Touch sidebar swatch) onto the canvas to trigger a flood fill (existing Krita behavior in `KisView::dropEvent()`).
  - while dragging **in Touch Mode**, slide horizontally to adjust threshold (1–100) with an on-canvas overlay; the chosen value persists to Fill Tool config (`KritaFill/KisToolFill` → `thresholdAmount`).
- ColorDrop parity (v2):
  - “Continue filling” + recolor flows (deferred).

---

## 7) Milestones

### M0 — Environment works

DoD:

- Docker image builds
- container runs
- Krita builds + installs
- Xvfb screenshot smoke produces non-empty PNG

### M1 — Touch Mode baseline (X11)

DoD:

- Finger = gestures only by default
- Navigation gestures reliable on real X11 touchscreen hardware

### M2 — Procreate-like chrome v1

DoD:

- Workspace exists (left/right)
- Touch sidebar exists (even if visually rough)

### M3 — Selection parity v1

DoD:

- `KisToolSelectTouch` is usable touch-first and covers the core selection modes

### M4 — Transform parity v1

DoD:

- Transform doesn’t fight navigation gestures

### M5 — QuickMenu + Copy/Paste overlays

DoD:

- Overlays are accessible in touch mode and canvas-only

### M8 — Android bring-up + CI headless smoke

DoD:

- `arm64-v8a` APK built
- Genymotion Cloud CI smoke runs install+launch+screenshot+logcat

### M9 — Installable artifacts (AppImage + APK)

DoD:

- Linux: AppImage built and launches.
- Android: `arm64-v8a` APK built and installs.
- Smoke suite can be run against the shipped artifacts.

---

## 8) Implementation Playbooks (P0/P1)

This section is the actionable “build checklist” per feature: what Procreate does, what we’ll ship, where the code lives, and what to test.

### P0.1 Touch Mode (toggle + workspace)

Procreate method (conceptual):

- Touch-first UI layout is the default.

What we ship:

- A single toggle: **Touch Mode**.
- Enabling Touch Mode:
  - loads a touch workspace (`Touch Procreate-like` or right-handed variant)
  - shows touch-specific dockers
  - hides desktop chrome (menu bar, status bar, non-touch toolbars)
  - switches to the touch-first palette theme (**Touch Procreate Dark**)
- Disabling Touch Mode:
  - restores prior workspace layout

Code checkpoints:

- `libs/ui/kis_config.{h,cc}`
  - `touchModeEnabled` persistence
- `libs/ui/KisMainWindow.{h,cpp}`
  - create `touch_mode_enabled` action
  - `slotTouchModeToggled(bool)`
  - `applyTouchMode(bool)`
  - workspace load/restore
  - theme + chrome toggles
- Android-specific chrome suppression:
  - Problem: the XMLGUI layer can re-apply mainwindow settings *after* we hide chrome, and on Android it forces the menubar config to `Enabled`.
  - Fix:
    - `libs/ui/KisMainWindow.cpp`: sets property `_krita_touch_hide_chrome` when Touch Mode is enabled.
    - `libs/widgetutils/xmlgui/kmainwindow.cpp`: `applyMainWindowSettings()` re-hides menu/status/toolbars when `_krita_touch_hide_chrome` is set, after the `restoreState()` call.
- Workspace assets:
  - `krita/data/workspaces/Touch_Procreate_like*.kws`
  - `krita/data/workspaces/CMakeLists.txt`
- Theme asset:
  - `krita/data/themes/TouchProcreateDark.colors`
  - `krita/data/themes/CMakeLists.txt`

Headless smoke:

- Linux: `./bin/krita-xvfb-screenshot -- --workspace "Touch Procreate-like"`

Manual acceptance checklist:

- Touch Mode on/off does not destroy previous layout.
- Touch Mode makes touch-first surfaces visible.
- Touch Mode hides menu bar + status bar; disabling restores them.
- In Touch Mode, dock widgets have **no title bars** (Procreate-like, no close/float affordances).
- In Touch Mode, the document canvas has **no MDI subwindow title strip** (no `[Not Saved]` bar / close button).
- In Touch Mode, canvas scrollbars are hidden (navigation is via gestures, Procreate-like).

Implementation note:

- We suppress dock chrome (title bar + docking features) in `libs/ui/KisMainWindow.cpp` (`applyTouchMode()`), and restore it when leaving Touch Mode.
- We also hide the QMdiArea tab/title strip in Touch Mode so the canvas is full-bleed (no `[Not Saved]` bar / close button).
- We hide the canvas scrollbars in Touch Mode via `libs/ui/KisViewManager.cpp` (`showHideScrollbars()`), and also force the `QMdiArea` scrollbars off in Touch Mode via `libs/ui/KisMainWindow.cpp` (Touch chrome helpers).

### P0.2 Selection parity v1 — `KisToolSelectTouch`

Procreate behavior summary:

- Mode buttons: Automatic / Freehand / Rectangle / Ellipse.
- Freehand supports:
  - drag lasso (close loop)
  - tap-to-polygon (tap points; close near first)
- Automatic supports threshold tuning.
- Operations in the selection toolbar:
  - Add / Remove / Invert
  - Feather (slider)
  - Save & Load
  - Color Fill / Clear
  - Copy & Paste (duplicates selected contents into a new layer)

What we ship in v1:

- Single “Touch Selection Tool” with 4 modes.
- Works with selection actions: add/subtract/intersect.
- Touch-friendly option UI (big, minimal).
- Touch-friendly selection action strip + feather slider (Procreate-like).
- Touch-friendly **Save / Load** (single-slot, in-memory) to quickly reuse a selection inside a session.
- Close-to-parity “selection toolbar” for touch:
  - keep the operations reachable without menus
  - map operations to existing Krita actions where possible

Code checkpoints:

- Tool implementation:
  - `plugins/tools/selectiontools/kis_tool_select_touch.{h,cc}`
- Tool registration + install:
  - `plugins/tools/selectiontools/selection_tools.cc`
  - `plugins/tools/selectiontools/CMakeLists.txt`
  - `plugins/tools/selectiontools/KisToolSelectTouch.action`

Smoke scenario:

- Desktop: `--touch-smoke=selection-tool` switches to the tool.
- Android: intent extra `KRITA_TOUCH_SMOKE=selection-tool`.

Manual acceptance checklist:

- Freehand lasso selection works and closes reliably.
- Tap-to-polygon works; close gesture is reliable.
- Automatic selection is predictable and threshold UI is usable.
- Selection action strip works (Replace/Add/Subtract/Intersect).
- Feather slider visibly affects the selection edge.
- Save stores the current selection; Load restores it (Replace).
- Undo/redo behaves as expected.

### P0.3 Transform parity v1

Procreate behavior summary:

- Transform is a dedicated mode.
- Gestures have context:
  - pinch **inside** transform box scales content (uniform scale)
  - pinch **outside** transform box zooms/rotates the canvas view
  - holding Transform temporarily flips that routing (advanced but important “feel”)
- Nudge: tap outside box nudges content in that direction (step depends on zoom)
- Commit: tap Transform again

What we ship in v1:

- Touch mode ensures transform doesn’t fight navigation gestures.
- A touch-optimized transform “routing” rule-set:
  - when Transform tool is active, 2-finger gestures should preferentially manipulate transform if they start inside the bounding box
  - otherwise, they manipulate the canvas view

Code checkpoints (to refine during implementation):

- Smoke scenario:
  - `--touch-smoke=transform-tool` switches to `KisToolTransform`.
- Gesture routing:
  - Primary pinch+rotate handler:
    - `libs/ui/input/kis_zoom_and_rotate_action.{h,cpp}` (routes 2-finger gestures to transform tool when inside bounds; otherwise zoom/rotate view)
  - Fallback handlers (if a profile maps pinch/rotate separately):
    - `libs/ui/input/kis_zoom_action.{h,cpp}`
    - `libs/ui/input/kis_rotate_canvas_action.{h,cpp}`
- Transform tool internals:
  - `plugins/tools/tool_transform2/kis_tool_transform.{h,cc}`
    - `touchTransformHitTest(widgetPoint)` (hit-test against current transformed bounds)
    - `touchTransformGestureBegin/Update/End(p0,p1)` (apply uniform scale + rotation + translation from a 2-finger gesture)

Manual acceptance checklist:

- Enter transform with one tap.
- While active: pinch/rotate manipulates transform, not canvas.
- Exit transform cleanly.

### P0.4 QuickShape (QuickLine) — parity v1

Procreate behavior summary:

- Draw a shape and hold at the end to snap into a clean geometric shape.
- Tap a second finger to “perfect” the snapped shape and/or enter Edit Shape.

What we ship in v1 (scoped):

- Touch Mode-only **QuickLine**:
  - with a freehand brush tool active, draw a line-ish stroke and **hold at the end** (≥350ms) before lifting → replaces the stroke with a straight line drawn using the current brush.
- A single toggle in Gesture Controls:
  - config: `touchQuickShapeEnabled`
  - action: `touch_quickshape_enabled`

Code checkpoints:

- Toggle + UI:
  - `libs/ui/kis_config.{h,cc}` (`touchQuickShapeEnabled`)
  - `libs/ui/KisMainWindow.{h,cpp}` (`touch_quickshape_enabled`)
  - `libs/ui/widgets/kis_touch_actions_sheet.cpp` (Gestures page)
- QuickLine implementation:
  - `libs/ui/tool/kis_tool_freehand.{h,cc}`
    - records stroke points in pixel coordinates
    - detects “hold at end” via `QElapsedTimer`
    - if line-fit passes → waits for stroke render, undoes last command, repaints as a straight line via `KisFigurePaintingToolHelper`

Headless smoke:

- Linux: `--touch-smoke=quickshape` (draws a wobbly line + snaps it)
- Android: intent extra `KRITA_TOUCH_SMOKE=quickshape`

Manual acceptance checklist:

- In Touch Mode, drawing a wobbly line and holding at the end snaps to a straight line.
- Drawing and lifting immediately does not trigger QuickShape.
- Toggling QuickShape off disables snapping.
- Undo removes the snapped line (returns to prior canvas state).

Next (v2):

- **P0** — Add recognition + snapping for **circle/ellipse**
- **P0** — Add recognition + snapping for **rectangle**
- **P1** — Add recognition + snapping for **triangle**
- **P1** — Add recognition + snapping for **polygon**
- **P2** — Implement “second-finger tap to perfect”
- **P3** — Add an “Edit Shape” affordance (handles / mode)

### P1.1 Touch sidebar (size/opacity/undo/redo/modify)

Procreate behavior summary:

- Vertical sliders for size + opacity.
- Large undo/redo.
- Modify button (often eyedropper).
- While holding a slider, you can drag sideways for fine adjustments (high precision).
- Tap-and-hold undo/redo repeats.

What we ship:

- Implemented by evolving Krita’s existing Touch Docker:
  - large Undo/Redo
    - Undo/Redo repeat while held (auto-repeat)
  - vertical Size/Opacity sliders (bound to canvas resources)
    - touch fine control: drag sideways while holding for higher precision
  - large “Modify” button (v1 maps to Color Sampler tool)
  - Actions button (opens a touch-first Actions sheet overlay; v1)
- Left/right handed placement (Touch Mode uses `touchRightHanded`).
  - Toggled via action `touch_right_handed` (Actions sheet → Prefs).

Code checkpoints:

- Touch docker plugin + UI:
  - `plugins/dockers/touchdocker/TouchDocker.cpp` (factory id is `TouchDocker`)
  - `plugins/dockers/touchdocker/TouchDockerWidget.{ui,h,cpp}`
    - `btnActions` bound to action `touch_actions_sheet` (fallback `command_bar_open`)
    - `btnUndo/btnRedo` use button auto-repeat for press-and-hold undo/redo
    - `sliderSize/sliderOpacity` use a touch-only fine-control event filter
- Touch Mode placement:
  - `libs/ui/KisMainWindow.cpp` (`applyTouchMode`)
  - `libs/ui/KisMainWindow.cpp` action `touch_right_handed` (updates `touchRightHanded`)
- Deterministic smoke placement + screenshot stability:
  - `libs/ui/KisApplication.cpp` (`runTouchSmokeScenario`, `showDockerForTouchSmoke`)

Headless smoke:

- Linux: `--touch-smoke=touch-sidebar` (shows TouchDocker and sets deterministic width)

Manual acceptance checklist:

- Sliders are finger-usable.
- While holding a slider, dragging sideways provides fine control (higher precision).
- Sidebar is visible in canvas-only/fullscreen.
- Left/right handed toggle behaves.
- Modify button reliably activates eyedropper behavior (v1 = Color Sampler tool; later = Procreate-like temporary eyedropper).
- Tap-and-hold Undo/Redo repeats.
- Size/Opacity updates stay in sync when changed elsewhere (toolbar, presets, etc.).

### P1.2 QuickMenu overlay

Procreate behavior summary:

- QuickMenu is a radial menu with **6 configurable buttons**.
- Two interaction styles:
  - tap to open → tap a button to activate
  - touch-drag (press, slide, release) to select quickly (“flick”)
- Buttons are customizable via press-and-hold (swap each slot to another action).

What we ship:

- A touch-first QuickMenu overlay with 6 slots.
- Slots configurable via a touch-first **QuickMenu Setup** sheet (Actions sheet → Gestures).
- Slot assignments persisted in config (`touchQuickMenuActionIds`) and applied at gesture-open time.
- Long-hold on a highlighted slot opens QuickMenu Setup with that slot preselected (Procreate-like customization flow).
- Must not conflict with OS-level gestures on Android.

Code checkpoints:

- Touch hold action (opens on hold trigger; selects on slide; triggers on release):
  - `libs/ui/input/KisTouchQuickMenuAction.{h,cpp}`
- Overlay widget (input-transparent):
  - `libs/ui/widgets/kis_touch_quickmenu_overlay.{h,cpp}`
- Slot persistence:
  - `libs/ui/kis_config.{h,cc}` (`touchQuickMenuActionIds`)
- Slot configuration UI:
  - `libs/ui/widgets/kis_touch_quickmenu_config_sheet.{h,cpp}`
  - main window action id `touch_quickmenu_configure` in `libs/ui/KisMainWindow.{h,cpp}`
  - entry point: Actions sheet → Gestures → “QuickMenu Setup” (`libs/ui/widgets/kis_touch_actions_sheet.cpp`)
- Gesture binding (touch mode profile):
  - `krita/data/input/touch-gestures-only.profile` (`OneFingerHold` → `Touch QuickMenu`)
- Smoke:
  - Desktop: `--touch-smoke=quickmenu` (shows overlay at deterministic center)
  - Desktop: `--touch-smoke=quickmenu-setup` (opens the QuickMenu Setup sheet)
- Defaults (reset):
  - Undo / Redo / Select / Transform / Deselect / Canvas-only

Manual acceptance checklist:

- Fast to invoke.
- Slide-to-select is reliable.
- Works in canvas-only.
- Slots can be reassigned from the QuickMenu Setup sheet and persist across restarts.
- Long-hold to customize does not accidentally trigger the slot action.

### P1.3 Copy/Paste overlay

Procreate behavior summary:

- Three-finger swipe down reveals a small overlay with clipboard actions.
- Options include Cut, Copy, Copy All, Paste, Duplicate, Cut & Paste.

What we ship:

- A touch overlay bound to the Procreate gesture that triggers existing Krita actions:
  - `edit_cut`, `edit_copy`, `edit_paste`, `copy_merged`
  - `copy_selection_to_new_layer` (Procreate “Copy & Paste”)
  - `cut_selection_to_new_layer` (Procreate “Cut & Paste”)
  - `Duplicate`:
    - if a selection exists → `copy_selection_to_new_layer`
    - else → `duplicatelayer`

Code checkpoints:

- Gesture binding:
  - `krita/data/input/touch-gestures-only.profile` (`ThreeFingerDrag` → Copy/Paste overlay)
- Touch gesture dispatch:
  - `libs/ui/input/KisTouchGestureAction.{h,cpp}`:
    - 3-finger swipe down → `touch_copypaste_overlay`
    - 3-finger scrub sideways → `clear` (if `touchClearLayerGestureEnabled`)
- Overlay widget:
  - `libs/ui/widgets/kis_touch_copypaste_overlay.{h,cpp}`
- MainWindow action wiring:
  - `libs/ui/KisMainWindow.{h,cpp}` (`touch_copypaste_overlay`)
- Headless smoke:
  - `--touch-smoke=copypaste` (shows overlay)

Manual acceptance checklist:

- Gesture invocation does not conflict with OS.
- Actions match expected behavior for selection vs layer contents.
- Duplicate behaves like Procreate:
  - when a selection exists, duplicate selection contents into a new layer
  - when no selection exists, duplicate current layer (or provide a clear alternative)

### P1.4 Colors (eyedropper + ColorDrop) — parity v1

Procreate behavior summary:

- Modify button provides a fast eyedropper flow.
- ColorDrop fills regions with threshold control while holding.

What we ship:

- v1 eyedropper parity:
  - Modify button = temporary `KisToolColorSampler` while pressed (release returns to prior tool)
  - While Modify is held in Touch Mode, temporarily force `touchPainting=Enabled` so one-finger drags are delivered to the sampler (not treated as pan gestures); restore the prior setting on release.
- v1 ColorDrop parity (scoped):
  - a draggable foreground color swatch in the Touch sidebar:
    - tap opens the existing color panel (placeholder: `ColorSelectorNg`)
    - drag-and-drop onto the canvas triggers a flood fill (ColorDrop-like)
  - fill uses existing Fill Tool defaults (threshold, anti-alias, etc.)
  - in Touch Mode, while dragging the color over the canvas:
    - slide horizontally to adjust fill threshold (1–100)
    - an on-canvas overlay shows the current threshold
    - the chosen threshold persists to Fill Tool config (`KritaFill/KisToolFill` → `thresholdAmount`)

Code checkpoints:

- Eyedropper:
  - `plugins/tools/basictools/kis_tool_colorsampler.{h,cc}` (`KritaSelected/KisToolColorSampler`)
  - `plugins/dockers/touchdocker/TouchDockerWidget.cpp` (Modify binding)
- ColorDrop / drag-fill plumbing:
  - Touch swatch source:
    - `plugins/dockers/touchdocker/TouchDockerWidget.ui` (`btnColor`)
    - `plugins/dockers/touchdocker/TouchDockerWidget.cpp` (opens `ColorSelectorNg`, keeps in sync with FG color)
  - Canvas drop handler (already supports color drop → fill using Fill Tool settings):
    - `libs/ui/KisView.cpp`:
      - `dropEvent()` branch for `mimeData()->hasColor()`
      - Touch Mode threshold adjust during drag:
        - `dragEnterEvent()` starts tracking and shows overlay
        - `dragMoveEvent()` updates threshold + overlay based on horizontal delta
        - `dropEvent()` applies the tracked threshold override
  - Fill Tool settings (shared defaults):
    - config group: `KritaFill/KisToolFill`
    - tool implementation: `plugins/tools/basictools/kis_tool_fill.{h,cc}` (`KritaFill/KisToolFill`)

Headless smoke:

- Linux: `--touch-smoke=color-panel` (currently shows existing color selector docker as a placeholder)
- Linux: `--touch-smoke=colordrop` (performs a ColorDrop fill)

Manual acceptance checklist:

- Modify is one-tap reachable and returns to painting immediately.
- Eyedropper works on-canvas and is accurate.
- ColorDrop-style fill is predictable and the threshold UI is touch-usable.

### P1.5 Layers panel gestures — parity v1

Procreate behavior summary:

- Layers list supports swipe gestures + quick options.
- Tap layer again opens a layer options sheet.

What we ship:

- A touch-friendly layers panel experience:
  - larger row height + touch targets
  - swipe gestures (v1 mapping):
    - swipe **left** on a layer row opens the **Layer Options** sheet (Lock / Duplicate / Delete)
    - swipe **right** on a layer row toggles multi-select (Procreate-style)
  - a touch-first layer options sheet (no right-click dependency)

Code checkpoints:

- Layer docker:
  - `plugins/dockers/layerdocker/LayerBox.{h,cpp}` (factory id `KisLayerBox`)
  - `plugins/dockers/layerdocker/NodeView.{h,cpp}` (layer list view events)
    - Touch Mode: tap the already-selected layer row to open `touch_layer_options_sheet`
    - Touch Mode: swipe left on a layer row triggers `touch_layer_options_sheet`
    - Touch Mode: swipe right on a layer row toggles row selection (multi-select)
    - Touch Mode: press-and-hold the visibility icon toggles “solo” visibility for that layer
- Touch layer options sheet:
  - overlay widget: `libs/ui/widgets/kis_touch_layer_options_sheet.{h,cpp}`
    - Includes an **Opacity** slider that updates the active node via `KisNodeManager::nodeOpacityChanged()`.
  - main window action wiring: `touch_layer_options_sheet` + `slotShowTouchLayerOptionsSheet()` in `libs/ui/KisMainWindow.{h,cpp}`
    - `slotShowTouchLayerOptionsSheet()` uses `sender()` action `data()` (optional `QPoint`) to open near the touch location
- Touch sizing (bigger rows/thumbnails when Touch Mode is enabled):
  - `libs/ui/kis_node_view_color_scheme.cpp` (uses `KisConfig::touchModeEnabled()` to scale sizes)

Headless smoke:

- Linux: `--touch-smoke=layers-panel` (shows Layers docker; validates swipe-right multi-select + hold-visibility solo/restore)
- Linux: `--touch-smoke=layer-options` (shows Layers docker and opens the touch Layer Options sheet)

Manual acceptance checklist:

- Layers panel is usable with fingers at tablet-scale UI sizes.
- In Touch Mode, rows/thumbnails/visibility targets are meaningfully larger.
- Tap the already-selected layer row opens the Layer Options sheet.
- Swipe left on a layer row opens the Layer Options sheet.
- Swipe right toggles selection on the swiped row (multi-select without modifiers).
- Press-and-hold the visibility icon toggles “solo” visibility for that layer.
- Layer Options sheet actions work and reflect state:
  - Opacity slider adjusts the active layer opacity.
  - Select Contents selects the current layer’s opaque pixels.
  - Alpha Lock / Lock toggles stay in sync with the layer state.
  - Duplicate duplicates the active layer.
  - Delete deletes the active layer (with whatever existing Krita confirmations apply).
- Most common layer operations are one-tap or one-swipe.
- No workflow requires right-click or tiny icons in Touch Mode.

### P1.6 Actions sheet (Procreate “wrench menu”) — parity v1

Procreate behavior summary:

- The Actions menu is a touch-first, categorized sheet (Add/Canvas/Share/Prefs/Help) with “Gesture Controls” under Prefs.

What we ship:

- A touch-first “Actions sheet” overlay that exposes:
  - file/canvas basics (new, open, export/share)
  - key prefs relevant to touch mode (gesture controls, full-screen behavior)
  - links into existing Krita dialogs for the rest
  - touch layout toggles: `touch_mode_enabled`, `touch_right_handed`
  - a dedicated **Gestures** page for the touch-first toggles:
    - touch painting mode (Auto / Enabled / Disabled)
    - rotate-with-pinch
    - quick pinch fit
    - QuickShape enable
    - QuickMenu enable
    - QuickMenu setup (slot assignment)
    - Undo/Redo gestures enable
    - Copy/Paste gesture enable
    - Clear Layer (3-finger scrub) enable
    - canvas-only gesture enable

Code checkpoints:

- Overlay widget:
  - `libs/ui/widgets/kis_touch_actions_sheet.{h,cpp}`
- Main window wiring:
  - action id: `touch_actions_sheet` in `libs/ui/KisMainWindow.cpp`
  - slot: `slotShowTouchActionsSheet()` in `libs/ui/KisMainWindow.{h,cpp}`
- Touch docker entry point:
  - `plugins/dockers/touchdocker/TouchDockerWidget.ui` (`btnActions`)
  - `plugins/dockers/touchdocker/TouchDockerWidget.cpp` (binds to `touch_actions_sheet`, fallback `command_bar_open`)
- Smoke:
  - `--touch-smoke=actions-sheet` triggers `touch_actions_sheet` (fallback `command_bar_open`)
  - `--touch-smoke=gesture-controls` validates rotate-with-pinch + clipboard + undo/redo + quickmenu + fullscreen gating and opens the Actions sheet on the **Gestures** page

Manual acceptance checklist:

- Actions sheet is reachable with one tap in Touch Mode.
- Categories are finger-sized and can be navigated without precision tapping.
- Core tasks (new doc, export) are achievable without desktop menus/toolbars.
- Gesture toggles actually change behavior (rotate-with-pinch, QuickShape, QuickMenu, clipboard, undo/redo, canvas-only).
- Quick Pinch Fit triggers `toggle_zoom_to_fit` and is reliable.

### P1.7 Touch top bar (“chrome”) — parity v1

Procreate behavior summary:

- A persistent top bar with two clusters (left = Actions/Selection/Transform, right = Brush/Layers/Color).

What we ship:

- A Touch Mode-only top toolbar (`touchTopBar`) with a minimal, Procreate-inspired set of buttons:
  - Left cluster:
    - Actions sheet (`touch_actions_sheet`)
    - Touch Selection Tool (`KisToolSelectTouch`)
    - Transform (`KisToolTransform`)
  - Right cluster:
    - Brush tool (`KritaShape/KisToolBrush`)
    - Eraser preset toggle (`eraser_preset_action`)
    - Layers panel toggle (docker `KisLayerBox`)
    - Color panel toggle (docker `ColorSelectorNg`)

Icon policy (touch-first):

- The top bar is **icon-only** (no text) with an icon size of ~40px.
- Some Krita `toggleViewAction()` actions can lack icons depending on platform/plugin order.
  - Fix: in `applyTouchMode()`, assign fallback icons if needed:
    - `touch_actions_sheet` → `config-performance` (gear)
    - Layers docker toggle (`KisLayerBox`) → `all-layers`
    - Color docker toggle (`ColorSelectorNg`) → `extended_color_selector`
- Target: Android and Linux screenshots show **only icons**, no “Actions / Layers / Advanced Color Selector” text.

Code checkpoints:

- Toolbar creation + show/hide in Touch Mode:
  - `libs/ui/KisMainWindow.cpp` (`applyTouchMode()`)
- Headless smoke:
  - `libs/ui/KisApplication.cpp` (`--touch-smoke=top-bar`)

Manual acceptance checklist:

- Top bar appears when Touch Mode is enabled and hides when Touch Mode is disabled.
- Buttons are usable with fingers and switch/toggle the intended UI/tool.
- Layers/Color buttons toggle their respective dockers reliably.
- On Android, the top bar uses **icons**, not text labels.

---

## 9) Open Questions

- What are the primary validation devices (exact Linux touchscreen + Android tablet models)?
- Do we standardize gestures across Linux+Android, or have platform-tuned defaults?
- How deep is ColorDrop parity (threshold fill + recolor), versus using Krita’s fill tool flows?
