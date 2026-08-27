# Godot 4.5 WeChat Mini Game Patchset

This bundle ports the committed WeChat Mini Game adapter from downstream commit `e95971c17e4890b24d52824eb1d045f7730f8771` to the exact official Godot base `6ce3de25aa58466e14ef354703ba8d9791a417da` (`4.5.2-stable`).

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
--include-optional wechat-glx
```

`export-api` is needed by the toolkit's scripted export flow. `branding` changes version metadata only. `dev-types` adds WeChat API declarations and does not affect the build. `wechat-glx` installs the official EmscriptenGLX static library and adds an opt-in Web build switch.

Install the GLX-capable adapter with:

```powershell
python adapter\scripts\apply_godot_patchset.py --include-optional wechat-glx
```

## Build

The verified toolchain is Emscripten `4.0.10`:

```powershell
# Standard Web/WeChat build without GLX.
scons platform=web target=template_release threads=no wasm_simd=no

# GLX build; requires --include-optional wechat-glx during patch installation.
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes

# GLX without C++ exceptions (~1.14 MiB smaller; aborts if the GLX library throws).
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes wechat_glx_exceptions=no

cmd /c compress_wasm.bat
```

`wechat_glx_exceptions` 默认 `yes`。`no` 用于完成发布的小游戏瘦身，测试阶段请保持 `yes`；体积数据与取舍见 `adapter/WECHAT_GLX.md`。裁切清单（`adapter/configs/*.py`）与异常模式也可通过 `python adapter/ci/package.py --template 4.5.2 --profile 2d --exceptions disabled` 及中央模板 workflow 输入选择。

`compress_wasm.bat` creates `bin/.web_zip/godot.wasm.br` and patches generated `godot.js`. Do not distribute a build that skipped this step.

The Mini Game host must load `godot-sdk.js`, then `godot-loader.js`, then generated `godot.js`.

## Scope

Core contains build/runtime wiring, WXMEMFS, `wx.request`, native long-audio handling, display/input adaptation, and the generated-JS postprocessor. Unused `audio.worker.js`, deprecated `library_godot_wx_audio.js`, local files, and historical artifacts are intentionally excluded.

The engine/runtime files match adapter commit `e95971c17e`. The packaged compression and post-processing helpers are deliberately hardened to stop on missing tools, missing build outputs, or missing required generated-JS anchors.
