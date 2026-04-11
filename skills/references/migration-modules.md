# Migration Modules

Use this file to understand what the public package actually ships. This is no longer a private-donor migration memo. It is the contract for the open-source bundle.

## Table Of Contents

1. Module order
2. Public package map
3. Core bundle
4. Optional bundle
5. Excluded donor features
6. Validation intent

## 1. Module Order

Always work in this order:

1. Verify upstream base
2. Apply core public bundle
3. Add optional modules only if the target really uses them
4. Vendor the runtime shell into the downstream minigame host
5. Build and validate

## 2. Public Package Map

| Module | Shipped as | Target files | Required | Why it exists |
| --- | --- | --- | --- | --- |
| Core build and runtime glue | `patches/.../core/001-build-and-runtime-glue.patch` | `modules/raycast/SCsub`, `platform/web/SCsub`, `platform/web/detect.py`, `platform/web/javascript_bridge_singleton.cpp`, `platform/web/js/engine/{config.js,engine.js,features.js,preloader.js}`, `platform/web/js/libs/{library_godot_javascript_singleton.js,library_godot_os.js}` | Yes | Aligns official Godot with WeChat runtime assumptions and bundled source files |
| File system and persistence | `sources/.../platform/web/js/libs/library_godot_fs.js` plus `godot_process.js` | `platform/web/js/libs/library_godot_fs.js`, repo-root `godot_process.js` | Yes | Ships the public readable WXMEMFS implementation under the actual filename loaded by `platform/web/SCsub` so `user://` works without donor access |
| HTTP and download pipeline | `sources/.../platform/web/js/libs/library_godot_fetch.js` plus engine patch | `platform/web/js/libs/library_godot_fetch.js`, `platform/web/js/engine/{engine.js,preloader.js,config.js}` | Yes | Replaces browser fetch assumptions with WeChat-compatible loading and chunk handling |
| Audio runtime | `sources/.../platform/web/js/libs/library_godot_audio.js` | `platform/web/js/libs/library_godot_audio.js` | Yes | Ships the current stable WeChat audio path with pooled context cleanup |
| Display and input | `sources/.../platform/web/js/libs/{library_godot_display.js,library_godot_input.js}` | `platform/web/js/libs/library_godot_display.js`, `platform/web/js/libs/library_godot_input.js` | Yes | Fixes DPI, window sizing, and keyboard bridge behavior |
| Runtime shell | `assets/min-runtime/*` and `scripts/install_min_runtime.py` | downstream host project files `godot-sdk.js`, `godot-loader.js` | Yes for host runtime | Removes dependency on the old machine-local `godot-minigame-sdk` |
| Packaging helpers | `sources/.../{compress_wasm.bat,compress_wasm.sh,godot_process.js}` | repo-root helper scripts | Yes | Post-processes generated `godot.js` and produces `.wasm.br` |
| Blob/Crypto helpers | `sources/.../platform/web/js/libs/{library_blob.js,library_godot_crypto.js}` | `platform/web/js/libs/library_blob.js`, `platform/web/js/libs/library_godot_crypto.js` | Yes | Supplies browser-missing APIs the WeChat runtime path depends on |

For the file-system layer, the public bundle now ships a single public file:

- `library_godot_fs.js`

Its contents are the readable WXMEMFS implementation used by the public package.

## 3. Core Bundle

### Core patch series

- `patches/godot-4.6.2-rc-a16e481cf4/core/series.txt`
- `patches/godot-4.6.2-rc-a16e481cf4/core/001-build-and-runtime-glue.patch`

### Core source root

- `sources/godot-4.6.2-rc-a16e481cf4/`

### What the core bundle changes

- switches WebAssembly loading to `.wasm.br`
- defaults `wasm_simd` to off and gates `-msimd128`
- disables `WASM_BIGINT`
- routes file loading through `fsUtils.localFetch`
- uses `wx.exitMiniProgram()` on exit
- exposes WeChat feature checks in `library_godot_os.js`
- disables `JavaScriptBridge::eval(...)` in WeChat
- preloads the public blob/crypto/fs files before normal web libraries

### Search anchors

```powershell
rg -n "WXMEMFS|fsUtils.localFetch|wx.exitMiniProgram|wasm\\.br|wasm_simd|WASM_BIGINT|wechat|minigame|wxgame" platform/web modules
```

## 4. Optional Bundle

These do not ship in the core path because they are not required for the default public bundle to work.

### `audio-worker`

- sources:
  - `sources/godot-4.6.2-rc-a16e481cf4/optional/platform/web/js/libs/audio.worker.js`
- patches:
  - `patches/godot-4.6.2-rc-a16e481cf4/optional/001-audio-worker.patch`
- use it only when the target runtime still packages or references `audio.worker.js`

### `dev-types`

- sources:
  - `sources/godot-4.6.2-rc-a16e481cf4/optional/platform/web/js/libs/lib.wx.api.d.ts`
- no patch
- use it when editor tooling or local type-checking needs WeChat API declarations

## 5. Excluded Donor Features

These were intentionally not bundled into the default open-source path:

- branding-only changes such as `version.py`
- donor-local docs and internal notes
- test assets such as `platform/web/js/tests/audio-unity.js`
- stale historical runtime routes such as `library_godot_wx_audio.js` as the primary audio entry

If a future public package needs one of these, ship it as a separate optional module instead of silently depending on donor history.

## 6. Validation Intent

The public package is considered healthy only when all of these are true:

- the exact-base apply works on the supported upstream commit
- a downstream host can vendor `godot-sdk.js` and `godot-loader.js` without the old external SDK
- `user://` persistence works through WXMEMFS
- large downloads are byte-correct
- repeated play/stop/replay audio is stable
- high-DPI sizing and keyboard input behave correctly
- exit uses `wx.exitMiniProgram()`
