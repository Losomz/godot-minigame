# Godot 4.5 WeChat Mini Game Patchset

This bundle reproduces the committed WeChat Mini Game adapter from downstream commit `e95971c17e4890b24d52824eb1d045f7730f8771` on the exact official Godot base `d1996aadb3672d877a7ae5df772b45127cf2c77a`.

## Apply

```powershell
git submodule update --init godot
python adapter\scripts\apply_godot_patchset.py
```

The apply script rejects dirty trees and mismatched bases by default. This is deliberate: the patchset is a reproducible development baseline, not a best-effort patch for arbitrary 4.5 revisions.

## Optional Features

Add only what the downstream project needs:

```powershell
--include-optional export-api
--include-optional branding
--include-optional dev-types
```

`export-api` is needed by the toolkit's scripted export flow. `branding` changes version metadata only. `dev-types` adds WeChat API declarations and does not affect the build.

## Build

The verified toolchain is Emscripten `4.0.10`:

```powershell
scons platform=web target=template_release threads=no wasm_simd=no
cmd /c compress_wasm.bat
```

`compress_wasm.bat` creates `bin/.web_zip/godot.wasm.br` and patches generated `godot.js`. Do not distribute a build that skipped this step.

The Mini Game host must load `godot-sdk.js`, then `godot-loader.js`, then generated `godot.js`.

## Scope

Core contains build/runtime wiring, WXMEMFS, `wx.request`, native long-audio handling, display/input adaptation, and the generated-JS postprocessor. Unused `audio.worker.js`, deprecated `library_godot_wx_audio.js`, local files, and historical artifacts are intentionally excluded.

The engine/runtime files match adapter commit `e95971c17e`. The packaged compression and post-processing helpers are deliberately hardened to stop on missing tools, missing build outputs, or missing required generated-JS anchors.
