---
name: godot-wechat-minigame-adapter
description: Apply the bundled self-contained Godot WeChat Mini Game adapter kit to an official Godot checkout. Use when the user wants to port official Godot to WeChat Mini Game, refresh a nearby 4.6-based port, or inspect the shipped patch/source bundle for WXMEMFS, `wx.request`, audio cleanup, `wx.getWindowInfo`, `wx.showKeyboard`, `.wasm.br`, and `wx.exitMiniProgram`. This skill is version-locked to the bundled upstream base and must not be described as a generic “Godot 4.x” patch dump.
---

# Godot WeChat Minigame Adapter

This skill is an open-source, self-contained adapter kit. It must work from:

- an official Godot checkout at the bundled base commit
- this skill directory

Do not rely on any private donor repo. If the target repo is not aligned to the bundled base, say that clearly and switch to a delta-refresh workflow instead of claiming drop-in support.

## Supported Base

- Primary supported upstream: `origin/4.6` at commit `a16e481cf424f8e39dc2cdea1a6bdc1e309acdc1`
- Public bundle id: `godot-4.6.2-rc-a16e481cf4`

Read `references/compatibility-matrix.md` before editing if the target is not exactly on that base.

## Start Mode

Pick one mode before editing:

1. **Exact-base apply**
   - Recommended.
   - The target repo is an official Godot checkout at `a16e481cf424f8e39dc2cdea1a6bdc1e309acdc1`.
   - Run the bundled apply script first.

2. **Near-base refresh**
   - The target repo is close to the bundled base, but not exact.
   - Use the bundled sources and patches as the public source of truth.
   - Expect manual conflict resolution and replay only after checking `references/recent-commit-deltas.md`.

## Hard Rules

- Treat `patches/` and `sources/` in this skill as the authoritative public package.
- Apply core first. Optional modules come later.
- Do not pull missing code from `C:\toolkit\godot4-custom` or any other machine-local repo.
- Do not describe this skill as “supports all Godot 4.x” or “works on any 4.6 build” unless you actually verified that target.
- Keep donor-only features out of the default public path.

## Workflow

### 1. Verify the Target

- Confirm the repo root and current commit:
  ```powershell
  git rev-parse HEAD
  git status --short
  ```
- If the target is on the supported base, use exact-base apply.
- If not, state the mismatch and read `references/compatibility-matrix.md`.

### 2. Apply the Public Bundle

Run from anywhere:

```powershell
python <skill-dir>\scripts\apply_godot_patchset.py <target-repo>
```

Useful flags:

- `--include-optional audio-worker`
- `--include-optional dev-types`
- `--allow-base-mismatch`
- `--allow-dirty`

The script copies the bundled public source files, then applies the bundled patch series.

### 3. Vendor the Runtime Shell

If the downstream minigame host project still expects the old external SDK globals:

```powershell
python <skill-dir>\scripts\install_min_runtime.py <dest-dir>
```

Then load:

1. `godot-sdk.js`
2. `godot-loader.js`
3. generated `godot.js`

### 4. Build and Package

Run from the target Godot repo root:

```powershell
scons platform=web target=template_release threads=no wasm_simd=no
node <skill-dir>\scripts\godot_process.js
cmd /c <skill-dir>\scripts\compress_wasm.bat
```

On Unix-like shells:

```bash
sh <skill-dir>/scripts/compress_wasm.sh
```

### 5. Validate

- Read `references/validation-checklist.md`.
- Do not call the port complete until the runtime shell, WXMEMFS persistence, large fetches, audio replay, display/input, and exit path all pass.

## Package Layout

- `patches/godot-4.6.2-rc-a16e481cf4/`
  - Version-locked patch bundle for the supported upstream base.
- `sources/godot-4.6.2-rc-a16e481cf4/`
  - Public source files copied into the target repo before patch application.
- `assets/min-runtime/`
  - Minimal replacement for the old external `godot-minigame-sdk`.

Read `references/migration-modules.md` for the exact core vs optional module split.

## References

- `references/compatibility-matrix.md`
  - Supported base, near-base policy, and intentionally excluded donor features.
- `references/migration-modules.md`
  - Public package map: which files are shipped as source, which ship as patch, and which are optional.
- `references/recent-commit-deltas.md`
  - Recent deltas already baked into the bundled base, plus what to replay when moving forward from it.
- `references/runtime-shell.md`
  - The minimal runtime shell contract for `godot-sdk.js` and `godot-loader.js`.
- `references/validation-checklist.md`
  - Build, packaging, and smoke-test gates.

## Output Standard

When using this skill on a real repo, always leave behind:

- the exact upstream commit or mismatch you worked against
- the apply command you ran
- which optional modules were included, if any
- the exact validation steps run
- any patch rejects or manual conflict resolutions
- any intentionally skipped donor features that remain out of scope
