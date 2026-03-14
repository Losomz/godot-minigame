---
name: godot-wechat-minigame-adapter
description: Port a fresh official Godot 4.x checkout to WeChat Mini Game / 微信小游戏, or refresh an existing port after upstream sync. Use when adapting `platform/web`, replaying proven WeChat runtime patches, auditing moved files across Godot versions, or fixing WeChat-specific runtime issues such as WXMEMFS persistence, `wx.request` chunk handling, `wasm_simd` gating, `wx.getWindowInfo`, `wx.showKeyboard`, `.wasm.br` loading, and `wx.exitMiniProgram`.
---

# Godot WeChat Minigame Adapter

Use this skill as a migration workflow, not as a loose patch dump. The goal is to let an AI or human take a clean upstream Godot checkout and port it to WeChat Mini Game in a controlled order, with clear module ownership and targeted verification after each module.

## Start Mode

Pick one mode before editing:

1. **Fresh port**
   - You have an official Godot repo with no WeChat work.
   - Follow the module order in `references/migration-modules.md`.
   - Rebuild and validate after each module instead of landing one giant patch.

2. **Delta refresh**
   - You already have a WeChat branch and just merged or rebased upstream.
   - First search for moved symbols with:
     ```powershell
     rg -n "WXMEMFS|enableChunked|onChunkReceived|getWindowInfo|showKeyboard|wx.exitMiniProgram|wasm_simd" platform/web modules version.py
     ```
   - Then replay only the missing module changes.
   - Read `references/recent-commit-deltas.md` before touching 4.6+ runtime compatibility.

## Hard Rules

- Port in module order: build/package -> file system -> network -> audio -> display/input -> runtime glue -> optional export and branding.
- Validate each module before moving on. Do not wait until the end to find the first breakage.
- Prefer readable donor sources over shipping copies. In the current donor repo, `platform/web/js/libs/library_godot_memfs.js` is the semantic source for WXMEMFS; `library_godot_fs.js` is a shipping copy and harder to reason about.
- Do not reintroduce deprecated file names. The active audio entry is `library_godot_audio.js`, not `library_godot_wx_audio.js`.
- Treat `audio.worker.js` as optional unless the target branch still references it. Current 4.6 runtime logic lives primarily in `library_godot_audio.js`.
- If upstream moved files or refactored APIs, update the routing first and only then replay logic.
- When replacing the old external `godot-minigame-sdk`, vendor the minimal runtime files from `assets/min-runtime/` instead of carrying the old SDK directory.

## Workflow

### 1. Preflight

- Confirm the target branch and upstream base.
- Inspect the current web platform surface:
  ```powershell
  git diff --name-only origin/<upstream-branch>..HEAD
  rg -n "WXMEMFS|wx.request|enableChunked|getWindowInfo|showKeyboard|wx.exitMiniProgram|wasm_simd" platform/web modules version.py
  ```
- If you are porting from this donor repo, use the latest minigame-specific commits in `references/recent-commit-deltas.md` as the final delta layer, not as the starting point.

### 2. Port by Module

- Open `references/migration-modules.md`.
- Work module by module.
- For each module:
  1. Port the owned files.
  2. Search the anchor strings listed for that module.
  3. Run the module-specific verification.
  4. Only continue if that module is green.

### 3. Replay Recent Compatibility Deltas

- After the baseline port works, open `references/recent-commit-deltas.md`.
- Explicitly check whether the target branch is still missing:
  - fetch chunk copy `write_offset`
  - `wasm_simd` default off plus `-msimd128` gating
  - the audio/display/input cleanup bundle that moved the runtime back onto `library_godot_audio.js`

### 4. Build and Package

Run packaging from the target repo root. The bundled scripts assume the current working directory is the repo root.

- If the target still depends on external bootstrap files, first vendor the minimal runtime shell:
  ```powershell
  python <skill-dir>\scripts\install_min_runtime.py <dest-dir>
  ```
  Then load `godot-sdk.js` before `godot-loader.js`, and load both before generated `godot.js`.

- Patch generated JS:
  ```powershell
  node <skill-dir>\scripts\godot_process.js
  ```
- Compress WASM:
  ```powershell
  cmd /c <skill-dir>\scripts\compress_wasm.bat
  ```
  or
  ```bash
  sh <skill-dir>/scripts/compress_wasm.sh
  ```

### 5. Final Validation

- Open `references/validation-checklist.md`.
- Do not call the port complete until:
  - `.wasm.br` loading works
  - WXMEMFS save/load survives restart
  - large HTTP responses no longer corrupt body copies
  - audio play/stop/replay does not leak or crash
  - high-DPI and keyboard input behave correctly in WeChat

## References

- `references/migration-modules.md`
  - Read this first for full module ownership, required edits, stale-path warnings, and module-specific acceptance checks.
- `references/recent-commit-deltas.md`
  - Read this when targeting 4.6+ or when an older port works except for iOS/WebKit, large downloads, or recent input/audio regressions.
- `references/runtime-shell.md`
  - Read this when removing the external `godot-minigame-sdk` dependency while preserving the old global names.
- `references/validation-checklist.md`
  - Read this at the end or whenever a module needs a focused smoke test.

## Bundled Scripts

- `scripts/godot_process.js`
  - Post-processes generated `bin/.web_zip/godot.js`.
  - Replaces `IDBFS` mount with `WXMEMFS`, patches i64 accessors, swaps unsupported window-title behavior, and keeps the generated JS aligned with WeChat runtime constraints.
- `scripts/compress_wasm.bat`
- `scripts/compress_wasm.sh`
  - Compress `bin/.web_zip/godot.wasm` and then run the post-processor.
- `scripts/install_min_runtime.py`
  - Copies `assets/min-runtime/godot-sdk.js` and `assets/min-runtime/godot-loader.js` into a target directory.

## Bundled Runtime Assets

- `assets/min-runtime/godot-sdk.js`
  - Minimal compatibility shell that preserves `GameGlobal.GODOTSDK`, `fsUtils`, `__globalAdapter`, and `nowPolyfill`.
- `assets/min-runtime/godot-loader.js`
  - Independent loader script that preserves `GameGlobal.GodotLoader`.

## Output Standard

When using this skill on a real repo, always leave behind:

- a module-by-module change summary
- the exact validation steps run
- any upstream file moves or renamed anchors that forced deviations from the default route
- a short list of deltas that were intentionally skipped because the target branch did not use that code path
