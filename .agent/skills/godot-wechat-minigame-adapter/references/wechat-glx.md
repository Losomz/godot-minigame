# WeChat EmscriptenGLX Maintenance Reference

Read this file before changing GLX source, generated Web output, Loader context selection, or the 4.5.2 GLX TPZ.

## Fixed Baseline

- Repository Godot line: `4.5.2-stable`
- Official base: `6ce3de25aa58466e14ef354703ba8d9791a417da`
- Emscripten: exactly `4.0.10`
- WeChat EmscriptenGLX: `0.1.11`
- GLX alignment source: `citizenll/godot@3dec2ca498f7b9e1ce07b33f5fbe08741a1429e5`
- Bridge SHA-256: `7f08edb7d3e7f3badc6c8b520b12c7424a9e52b223407ded92ae8f700bc01a0e`
- Static library SHA-256: `c70b6255285aa4e5b987f18cf24d59d56227c67d44d60b734755f33f6a8ab33a`

Do not introduce Godot 4.6 engine behavior outside the GLX rendering chain. Do not bring in 4.7 changes, native audio/cache work, editor changes, or GDExtension changes as part of GLX maintenance.

## Sources Of Truth

Production and replay sources live in the parent repository:

- `adapter/patches/manifest.json`
- `adapter/patches/optional/003-wechat-glx.patch`
- `adapter/sources/godot_process.js`
- `adapter/sources/optional/platform/web/js/patches/patch_em_gl.js`
- `adapter/sources/optional/platform/web/js/tests/test_wechat_glx_runtime.js`
- `adapter/thirdparty/wechat-glx/`
- `adapter/assets/min-runtime/`

Applied source and build output live under `godot/`. Keep these pairs synchronized:

- `adapter/sources/godot_process.js` -> `godot/godot_process.js`
- optional `patch_em_gl.js` -> `godot/platform/web/js/patches/patch_em_gl.js`
- optional runtime test -> `godot/platform/web/js/tests/test_wechat_glx_runtime.js`

Generated `templates/.../engine/godot.js`, `godot.wasm.br`, and `.tpz` are never the primary fix location.

## Runtime Model

GLX changes frame ownership, not only the context name:

1. Loader probes support, creates the selected canvas context, and pins the mode in `GameGlobal.__godotMinigameWXGLXEnabled`.
2. `patch_em_gl.js` wraps Emscripten context creation and maps `webgl`/`webgl2` to `wxwebgl`/`wxwebgl2` only when GLX is selected.
3. The bridge validates that the returned context mode matches the pinned Loader mode.
4. Native exports initialize the GLX command buffer and context id.
5. WeChat's `registerAfterDoFrame` callback flushes `_glxCommandBufferFlush`.

Never silently mix a Loader-created standard context with an engine-requested GLX context. A pinned GLX creation failure must fail clearly instead of falling back on the same canvas.

The four GLX initialization logs prove only that native initialization ran. Success requires the Godot Renderer banner, a visible project scene, and stable continuous frames.

## Godot Changes And Rationale

`platform/web/detect.py`:

- Add opt-in `wechat_glx=yes`.
- Reject Emscripten versions other than 4.0.10.
- Link `thirdparty/wechat-glx/libemscriptenglx.a`.
- Enable C++ exceptions and `CHECK_NULL_WRITES=0`.
- Use `SUPPORT_LONGJMP='emscripten'` for GLX.
- Do not enable `OFFSCREEN_FRAMEBUFFER` for GLX.

`drivers/gles3/storage/utilities.cpp`:

- Compile out GLES timestamp Query allocation, deletion, submission, and result reads under `WECHAT_GLX_EXPERIMENTAL`.

`platform/web/display_server_web.cpp`:

- Set `explicitSwapControl=false` under GLX.
- Do not call `emscripten_webgl_commit_frame()` under GLX.

`platform/web/js/libs/library_godot_display.js`:

- Call `GL.resizeOffscreenFramebuffer` only when it exists.

`platform/web/js/patches/patch_em_gl.js`:

- Keep byte-identical to the pinned citizenll source unless intentionally updating the alignment commit.
- This is the only runtime bridge source. Do not restore generated-JS bridge injection.

These changes were validated as one stabilization set. Query/null-write are strong trigger candidates, but there was no single-variable device bisect. Do not document one line as the unique root cause.

## Postprocessor Boundary

`godot_process.js` remains a formal part of the WeChat toolchain. It handles deterministic differences in generated Emscripten JS, including invoke import mappings, host clock/filesystem compatibility, guarded `RuntimeError`, stat inode handling, and the plain callback wrapper required by WeChat.

Rules:

- A commit-frame shim may be added only if generated JS actually references `_emscripten_webgl_commit_frame`.
- GLX WASM must not import `emscripten_webgl_commit_frame`; GLX frame flush comes from `_glxCommandBufferFlush`.
- Do not inject `wxGLXGetNativeExport`, Query bypasses, mode fallback, or diagnostic probes from the postprocessor.
- Every transformation must be idempotent and covered by `adapter/tests/test_godot_process_glx.js` when it affects GLX output.

## Apply And Replay

From the official base, apply all requested optional features in one invocation:

```powershell
python adapter\scripts\apply_godot_patchset.py --include-optional wechat-glx
```

Add `--include-optional export-api` in the same command when needed. The installer expects the target checkout to be clean and at the manifest base. Do not use `--allow-base-mismatch` in a normal release flow.

Before committing, verify that a fresh replay produces the same maintained Godot files. At minimum compare:

- `platform/web/detect.py`
- `platform/web/display_server_web.cpp`
- `drivers/gles3/storage/utilities.cpp`
- `platform/web/js/libs/library_godot_display.js`
- `platform/web/js/patches/patch_em_gl.js`
- `platform/web/js/tests/test_wechat_glx_runtime.js`
- `godot_process.js`

## Build

Activate emsdk 4.0.10 and ensure Brotli 1.2.0 is first on `PATH`.

```powershell
Push-Location godot
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes
cmd /c compress_wasm.bat
Pop-Location
```

Expected SCons invariants in `godot/.scons_env.json`:

- `platform=web`
- `target=template_release`
- `threads=false`
- `wasm_simd=false`
- `wechat_glx=true`
- `disable_exceptions=false`
- `arch=wasm32`
- CXX flags include `-fexceptions`
- link flags include `CHECK_NULL_WRITES=0`, `ERROR_ON_UNDEFINED_SYMBOLS=0`, and Emscripten longjmp

Do not reuse a non-GLX WASM. A source commit changes embedded build identity and therefore normally changes the final hash even when runtime logic is unchanged.

## Required Tests

Run before the full build:

```powershell
node adapter\tests\test_godot_process_glx.js
node adapter\sources\optional\platform\web\js\tests\test_wechat_glx_runtime.js
node --check adapter\sources\godot_process.js
node --check adapter\sources\optional\platform\web\js\patches\patch_em_gl.js
```

The postprocessor test and runtime test have separate contracts. Keep both. The copy under `godot/platform/web/js/tests/` is the applied patchset result, not a second canonical test.

## WASM Contract

The final Brotli payload must decompress to `godot/bin/.web_zip/godot.wasm` and expose these nine functions:

- `glxUpdateContextId`
- `glxInit`
- `glxInitBufferDataAndGlState`
- `glxCommandBufferFlush`
- `glxShowOpenData`
- `glxHideOpenData`
- `glxGetVideoTempBuffer`
- `glxVideoUpdateToTexture`
- `glxVideoDestroy`

Required batch callbacks include `evalBatchRenderAsync`, `evalBatchRenderSync`, `evalBatchRenderSyncBool`, `evalBatchRenderSyncInt`, `evalBatchRenderSyncString`, and `evalBatchRenderArrayBuffer`.

Reject any final WASM that imports GLES Query functions or `emscripten_webgl_commit_frame`.

## Package

The Godot source must be committed and clean before packaging:

```powershell
python adapter\scripts\package_wechat_glx_template.py
```

The script requires `godot/` HEAD to match `wechat_glx_ref` in the manifest. It then runs the source tests, performs a clean SCons build, runs Brotli/JS postprocessing, validates source/build identity and the JS/WASM ABI, and updates:

- `templates/4.5.2/minigame4.5.2_glx/BUILD_INFO.md`
- `templates/4.5.2/minigame4.5.2_glx/godot-loader.js`
- `templates/4.5.2/minigame4.5.2_glx/engine/godot-sdk.js`
- `templates/4.5.2/minigame4.5.2_glx/engine/godot.js`
- `templates/4.5.2/minigame4.5.2_glx/engine/godot.wasm.br`
- `templates/4.5.2/minigame4.5.2_glx.tpz`

The TPZ is rebuilt from scratch with forward-slash entries at archive root. The script reopens it and compares every archived file with the unpacked template. Never update the old ZIP incrementally or add the template directory as an extra top-level entry. `--incremental` is for local iteration only; release packaging uses the default clean build.

Do not update `resources/versions.yaml` unless the user explicitly chooses to make this the downloadable default for Godot 4.5.2.

## Release Judgment

Static acceptance requires:

- patchset replay succeeds from the official base;
- source overlays match the applied Godot files;
- JS tests and syntax checks pass;
- bridge appears exactly once;
- no `WXGLX-DIAG`, generated Query bypass, or unused commit-frame shim remains;
- Brotli decompression and WASM imports/exports pass;
- unpacked template and TPZ match exactly.

Device acceptance requires Renderer banner, visible scene, and stable frames. Record the exact final TPZ hash. A predecessor candidate's device success supports the implementation but does not prove a newly rebuilt binary byte-for-byte; request a same-package smoke test before remote release.

Do not create or move Git tags, update release indexes, or push remotes unless the user explicitly requests those actions.
