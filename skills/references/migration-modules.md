# Migration Modules

Use this file to drive the actual port. Do not try to port the whole WeChat runtime in one diff. Work in the order below.

## Table Of Contents

1. Module order
2. Module map
3. External runtime shell
4. Build and packaging
5. File system and persistence
6. HTTP and download pipeline
7. Audio runtime
8. Display and input
9. Runtime glue and shutdown
10. Optional modules
11. Stale assumptions to avoid

## 1. Module Order

Always port in this order:

1. Build and packaging
2. External runtime shell
3. File system and persistence
4. HTTP and download pipeline
5. Audio runtime
6. Display and input
7. Runtime glue and shutdown
8. Optional export, branding, and module-specific compatibility work

## 2. Module Map

| Module | Owned files | Why it exists | Minimum proof |
| --- | --- | --- | --- |
| External runtime shell | `assets/min-runtime/{godot-sdk.js,godot-loader.js}`, `scripts/install_min_runtime.py` | Replaces the old machine-local `godot-minigame-sdk` while preserving the same bootstrap globals | Host project boots with the vendored runtime files and no external SDK path |
| Build and packaging | `platform/web/SCsub`, `platform/web/detect.py`, `platform/web/emscripten_helpers.py`, `platform/web/js/engine/{engine.js,preloader.js,config.js}`, `godot_process.js`, `compress_wasm.*` | Ensures WeChat-specific JS loads first, `.wasm.br` is used, runtime exits cleanly, and build flags do not assume browser-only features | Generated web template loads `.wasm.br` and `godot.js` is post-processed |
| File system and persistence | `platform/web/js/libs/library_godot_memfs.js`, `platform/web/js/libs/library_godot_fs.js`, `godot_process.js` | Replaces browser storage assumptions with WXMEMFS and makes `user://` reliable on WeChat | Save, read back immediately, restart, read again |
| HTTP and download pipeline | `platform/web/js/libs/library_godot_fetch.js`, `platform/web/js/engine/preloader.js`, `platform/web/js/engine/engine.js` | Replaces browser fetch assumptions with `wx.request` and chunk-safe buffer assembly | Large response body is correct and headers parse correctly |
| Audio runtime | `platform/web/js/libs/library_godot_audio.js`, `platform/web/SCsub`, `platform/web/js/libs/lib.wx.api.d.ts` | Adapts playback, cleanup, loop handling, and context reuse to WeChat behavior | Repeated play/stop/replay is stable |
| Display and input | `platform/web/js/libs/library_godot_display.js`, `platform/web/js/libs/library_godot_input.js`, `platform/web/js/libs/lib.wx.api.d.ts` | Fixes DPI, full-window sizing, touch coordinates, and IME/keyboard integration | Screen is crisp and text input works |
| Runtime glue and shutdown | `platform/web/javascript_bridge_singleton.cpp`, `platform/web/js/engine/config.js`, `version.py` | Disables unsafe JS eval paths and exits through WeChat lifecycle APIs | Exit returns to WeChat without browser-only assumptions |
| Optional modules | `modules/raycast/SCsub`, `editor/export/editor_export_preset.cpp`, `platform/web/emscripten_helpers.py`, downstream publish scripts | Handles version-specific flags, export automation, and optional worker packaging | Module-specific build or export behavior works |

## 3. External Runtime Shell

### Owned files

- `assets/min-runtime/godot-sdk.js`
- `assets/min-runtime/godot-loader.js`
- `scripts/install_min_runtime.py`

### Why this module exists

Current Godot code still expects a few globals from the old external `godot-minigame-sdk`. The minimal runtime shell keeps those names stable while removing the dependency on the old SDK directory.

### Must port

- Vendor `godot-sdk.js` and `godot-loader.js` into the target minigame host project.
- Keep these global names unchanged:
  - `GameGlobal.GODOTSDK`
  - `window/globalThis.fsUtils`
  - `window/globalThis.__globalAdapter`
  - `GameGlobal.nowPolyfill`
  - `GameGlobal.GodotLoader`
- Preserve only the minimal used API surface documented in `references/runtime-shell.md`.
- Do not copy the old full `godot-minigame-sdk` tree once these files are in place.

### Search anchors

```powershell
rg -n "GODOTSDK\\.|fsUtils\\.|__globalAdapter\\.|nowPolyfill|GodotLoader" platform/web modules
```

### Validate

- The target host loads the vendored `godot-sdk.js` and `godot-loader.js` successfully.
- No startup path still points at `C:\toolkit\godot-minigame-sdk`.
- `preloader.js`, `library_godot_audio.js`, and `library_godot_input.js` can resolve their globals.

## 4. Build And Packaging

### Owned files

- `platform/web/SCsub`
- `platform/web/detect.py`
- `platform/web/emscripten_helpers.py`
- `platform/web/js/engine/engine.js`
- `platform/web/js/engine/preloader.js`
- `platform/web/js/engine/config.js`
- `godot_process.js`
- `compress_wasm.bat`
- `compress_wasm.sh`

### Why this module exists

This module changes the web export pipeline from browser-default assumptions to WeChat-friendly runtime wiring. If this module is wrong, later module work may look broken even when those modules are correct.

### Must port

- In `platform/web/SCsub`, preload WeChat-specific JS before standard libraries:
  - `library_godot_crypto.js`
  - `library_blob.js`
  - `library_godot_fs.js`
- Keep the active audio entry on `library_godot_audio.js`.
- In `platform/web/detect.py`, keep `wasm_simd` opt-in and default it to `False`.
- Guard `-msimd128` with `if env["wasm_simd"]`.
- In `platform/web/js/engine/engine.js`, load `.wasm.br` instead of raw `.wasm` and keep the preloader exposed when the runtime needs it.
- In `platform/web/js/engine/preloader.js`, use `fsUtils.localFetch`.
- In `platform/web/js/engine/config.js`, exit via `wx.exitMiniProgram()` and keep the fallback instantiate path aligned with `.wasm.br`.
- In `godot_process.js`, patch the generated `godot.js` so `FS.mount(IDBFS,{},path)` becomes `FS.mount(WXMEMFS,{},path)` and apply the i64 and title patches.
- In `platform/web/emscripten_helpers.py`, only keep `audio.worker.js` packaging if the target runtime still references that worker path.

### Search anchors

```powershell
rg -n "library_godot_audio|library_godot_wx_audio|wasm_simd|fsUtils.localFetch|wx.exitMiniProgram|wasm.br|FS.mount\\(WXMEMFS|IDBFS" platform/web godot_process.js compress_wasm.*
```

### Validate

- Build a web template successfully.
- Confirm these anchors after build:
  ```powershell
  rg -n "wasm\\.br|wx.exitMiniProgram|FS.mount\\(WXMEMFS|nowPolyfill" bin/.web_zip/godot.js platform/web/js/engine
  ```
- Confirm `bin/.web_zip/godot.wasm.br` exists.

## 5. File System And Persistence

### Owned files

- `platform/web/js/libs/library_godot_memfs.js`
- `platform/web/js/libs/library_godot_fs.js`
- `godot_process.js`

### Why this module exists

Browser `IDBFS` semantics are not the same as WeChat persistence semantics. The current donor runtime uses WXMEMFS to get write-after-read correctness, close-time persistence, lazy loading, and controlled PCK retention.

### Must port

- Prefer `library_godot_memfs.js` as the readable donor source.
- Treat `library_godot_fs.js` as the shipping copy if the build expects that exact filename.
- Keep these behaviors:
  - `wx.env.USER_DATA_PATH` path mapping
  - lazy node load
  - dirty tracking on writes
  - persist on close
  - reference counts for open streams
  - PCK cache helpers such as `releasePck`, `getWxPath`, `getGodotPath`
- Keep the `godot_process.js` mount rewrite from `IDBFS` to `WXMEMFS`.

### Search anchors

```powershell
rg -n "WXMEMFS|USER_DATA_PATH|releasePck|getWxPath|getGodotPath|dirty|refCount|FS.mount\\(WXMEMFS" platform/web/js/libs godot_process.js
```

### Validate

- Save a file under `user://`, close it, reopen it immediately, and verify contents.
- Restart the game and verify the file still exists.
- Confirm no stale-read bug after a write in the same session.

## 6. HTTP And Download Pipeline

### Owned files

- `platform/web/js/libs/library_godot_fetch.js`
- `platform/web/js/engine/preloader.js`
- `platform/web/js/engine/engine.js`

### Why this module exists

WeChat uses `wx.request`, not browser fetch. The current donor runtime also relies on chunk callbacks, arraybuffer response bodies, and a manual read-copy path. This is where large-download corruption is easiest to reintroduce.

### Must port

- Replace browser fetch assumptions with `wx.request`.
- Keep:
  - `responseType: 'arraybuffer'`
  - `enableChunked: true`
  - `requestTask.onHeadersReceived(...)`
  - `requestTask.onChunkReceived(...)`
  - fallback to `res.data` when chunk callbacks do not fire
- Keep the cumulative `write_offset` in `godot_js_fetch_read_chunk`.

### Search anchors

```powershell
rg -n "wx.request|enableChunked|onHeadersReceived|onChunkReceived|bodySize|write_offset|heapCopy" platform/web/js/libs/library_godot_fetch.js
```

### Validate

- Download a payload larger than a single internal chunk and verify the reconstructed bytes are correct.
- Verify headers are readable through the Godot fetch bridge.
- Verify small payloads still work when WeChat skips chunk callbacks and only returns `res.data`.

## 7. Audio Runtime

### Owned files

- `platform/web/js/libs/library_godot_audio.js`
- `platform/web/SCsub`
- `platform/web/js/libs/lib.wx.api.d.ts`
- optionally `platform/web/js/libs/audio.worker.js`

### Why this module exists

Current 4.6 runtime stability comes from WeChat-specific playback cleanup and reusable audio contexts inside `library_godot_audio.js`, not from the older standalone `library_godot_wx_audio.js` path.

### Must port

- Keep `platform/web/SCsub` pointing at `library_godot_audio.js`.
- In `library_godot_audio.js`, preserve:
  - `MAX_POOL_SIZE`
  - `contextPool`
  - `releaseContext`
  - `resetContext`
  - `destroyContext`
  - `cleanupPlayback`
  - `ctx.loop = streamInfo.loopMode === "forward"`
  - `ctx.startTime = offset > 0 ? offset : 0`
  - cleanup on `onEnded`, `onStop`, and `onError`
- Only carry `audio.worker.js` if `rg -n "createWorker|audio.worker"` on the target runtime proves the worker path is still live.

### Search anchors

```powershell
rg -n "contextPool|cleanupPlayback|onStop|onError|startTime|loopMode|MAX_POOL_SIZE|createWorker|audio.worker" platform/web/js/libs/library_godot_audio.js platform/web/js/libs/audio.worker.js
```

### Validate

- Play, stop, and replay the same sound repeatedly without leaking contexts.
- Verify looping audio restarts correctly.
- Verify failing audio paths clean themselves up instead of leaving broken active playback entries.

## 8. Display And Input

### Owned files

- `platform/web/js/libs/library_godot_display.js`
- `platform/web/js/libs/library_godot_input.js`
- `platform/web/js/libs/lib.wx.api.d.ts`

### Why this module exists

WeChat screen metrics and keyboard APIs differ from browser DOM assumptions. This module fixes high-DPI sizing, window metrics, and text entry.

### Must port

- In `library_godot_display.js`, use `wx.getWindowInfo()` for pixel ratio, window width, and window height.
- Keep any full-window calculations aligned with `windowWidth`, `windowHeight`, and `pixelRatio`.
- In `library_godot_input.js`, keep the WeChat keyboard bridge based on `wx.showKeyboard`.
- Preserve the current IME logic that:
  - tracks last value and current length
  - ends IME before replaying key events
  - clears existing text before injecting the new value
  - handles both confirm and complete
- Keep `lib.wx.api.d.ts` updated so editor tooling matches actual WeChat APIs such as `getWindowInfo`.

### Search anchors

```powershell
rg -n "getWindowInfo|pixelRatio|windowWidth|windowHeight|showKeyboard|onKeyboardInput|onKeyboardConfirm|onKeyboardComplete|Backspace|composition" platform/web/js/libs
```

### Validate

- Confirm the game is sharp on high-DPI devices.
- Confirm full-window sizing is correct.
- Confirm text can be typed, replaced, confirmed, and dismissed without duplicate characters.

## 9. Runtime Glue And Shutdown

### Owned files

- `platform/web/javascript_bridge_singleton.cpp`
- `platform/web/js/engine/config.js`
- `version.py`

### Why this module exists

This module removes browser-only assumptions at the bridge boundary and makes lifecycle behavior fit WeChat.

### Must port

- Keep `JavaScriptBridge::eval(...)` returning an empty `Variant()` because `eval()` is not allowed in WeChat Mini Game.
- Keep `config.js` exit behavior on `wx.exitMiniProgram()`.
- Update `version.py` branding only after the runtime path is already stable.

### Search anchors

```powershell
rg -n "eval\\(|Variant\\(\\)|wx.exitMiniProgram|Godot Engine for Wechat|get.godots.app" platform/web/javascript_bridge_singleton.cpp platform/web/js/engine/config.js version.py
```

### Validate

- Runtime does not attempt unsafe JS eval.
- Exit path returns through WeChat lifecycle instead of browser unload behavior.
- Branding changes do not break version metadata.

## 10. Optional Modules

Port these only when the target repo uses them:

- `modules/raycast/SCsub`
  - Keep `-msimd128` behind `env["wasm_simd"]`.
- `editor/export/editor_export_preset.cpp`
  - Needed only when you also need export-pipeline automation on the Godot side.
- `platform/web/emscripten_helpers.py`
  - Keep worker packaging only if the current runtime still references `audio.worker.js`.
- downstream publish or branch-management scripts
  - These are workflow helpers, not runtime prerequisites.

## 11. Stale Assumptions To Avoid

Do not carry these forward from older notes or old skills:

- `library_godot_minigame_fs.js` is not the current 4.6 donor path.
  - Current donor logic lives in `library_godot_memfs.js` and the shipping `library_godot_fs.js`.
- `library_godot_wx_audio.js` is not the current audio entry.
  - Current runtime is on `library_godot_audio.js`.
- `audio.worker.js` is not automatically mandatory.
  - Keep it only if the target runtime still calls it.
- the whole external `godot-minigame-sdk` directory is still required.
  - Current skill ships a minimal replacement in `assets/min-runtime/`.
- `IDBFS` should remain mounted in generated JS.
  - Current post-process step rewrites this to `WXMEMFS`.
