# WXGLX Adaptation

Use this reference when forward-porting the current WeChat EmscriptenGLX implementation into a Godot Web source tree or validating a GLX-capable template.

## Scope And Status

The version-locked public bundle does not become GLX-capable merely because the runtime shell contains `wxwebgl2` selection logic. A GLX-capable engine also needs the matching vendor static library, build flags, context patch, native exports, runtime-mode bridge, and frame-presentation changes described below.

The current design produces one GLX-capable binary that chooses exactly one mode at startup:

- WXGLX: `wxwebgl2`, native GLX command submission, no Emscripten frame commit.
- Standard fallback: `webgl2`, explicit swap control, Emscripten frame commit plus WeChat `flush()` / `commit()`.

This is a startup decision, not a live toggle.

## Non-Negotiable Invariants

1. The loading canvas and Godot engine must use the same context mode.
2. The loader must set `GameGlobal.__godotMinigameWXGLXEnabled` before Godot creates its context.
3. `GameGlobal.__GODOT_DISABLE_WXGLX === true` must force standard WebGL before any context exists.
4. A pinned WXGLX mode must fail loudly if `wxwebgl2` creation or native GLX bindings are missing. Do not silently create `webgl2` after pinning WXGLX.
5. Frame presentation must follow the selected runtime mode, not the compile-time `WECHAT_GLX_EXPERIMENTAL` define.
6. Post-processing must preserve Emscripten's `_emscripten_webgl_commit_frame`; replacing it with an empty function causes black screens on standard-WebGL fallback devices.

## Engine Integration Map

### Build capability

Update `platform/web/detect.py` to provide `use_wx_glx` and `wx_glx_lib`.

When enabled:

- require an exact `libemscriptenglx_<emscripten-version>.a` match
- define `WECHAT_GLX_EXPERIMENTAL`
- keep C++ exceptions enabled
- use `SUPPORT_LONGJMP=emscripten`
- disable `OFFSCREEN_FRAMEBUFFER`
- export the runtime/native functions required by the GLX patch
- use `threads=no`

The verified production combination is Emscripten `4.0.10`, `threads=no`, and `wasm_simd=no`. Do not guess compatibility between an Emscripten version and a differently versioned static library.

Add `platform/web/js/patches/patch_em_gl.js` through `platform/web/SCsub`. The patch must:

- intercept `canvas.getContext()` while `GL.createContext()` runs
- request `wxwebgl` / `wxwebgl2` only when the pinned mode is WXGLX
- call `glxInit` immediately after context creation
- call `glxInitBufferDataAndGlState` once
- call `glxUpdateContextId` whenever the current context changes
- verify that the actual context type matches the pinned mode

### Runtime mode bridge

Add a synchronous JS bridge in `platform/web/js/libs/library_godot_display.js` that returns whether `GameGlobal.__godotMinigameWXGLXEnabled === true`. Declare it in `platform/web/godot_js.h`.

Store that value on `DisplayServerWeb`, then configure presentation from the runtime value:

```cpp
wx_glx_enabled = godot_js_display_is_wx_glx_enabled() == 1;
attributes.explicitSwapControl = !wx_glx_enabled;
```

Call `emscripten_webgl_commit_frame()` only when `wx_glx_enabled` is false, including normal swaps and context destruction. A compile-time-only branch is incorrect because the same GLX-capable binary can start in standard WebGL mode.

Guard `GL.resizeOffscreenFramebuffer()` when `OFFSCREEN_FRAMEBUFFER` is absent. Keep the existing GLX compatibility handling for GLES timestamp queries and unsupported runtime GDExtension loading if those paths exist in the target branch.

## Runtime Shell And Startup Configuration

Load configuration before `godot-loader.js`:

```js
const gameGlobal = typeof GameGlobal !== "undefined" ? GameGlobal : globalThis;

if (gameGlobal.__GODOT_DISABLE_WXGLX === undefined) {
    gameGlobal.__GODOT_DISABLE_WXGLX = false;
}
```

The import order must be:

```js
import "./weapp-adapter";
import "./glx-config";
import "./godot-loader";
```

The loader selects WXGLX only when all startup policy checks allow it:

```js
const useWXGLX =
    gameGlobal.__GODOT_DISABLE_WXGLX !== true &&
    typeof wx !== "undefined" &&
    wx.env &&
    wx.env.isSupportEmscriptenGLX;

gameGlobal.__godotMinigameWXGLXEnabled = !!useWXGLX;
```

Pair a GLX-selecting loader only with a GLX-capable engine. For a standard-only template, force `__GODOT_DISABLE_WXGLX = true` or use a loader that never requests `wxwebgl2`.

## Generated Glue And Frame Commit

Run `compress_wasm.bat` or `compress_wasm.sh` after every build. The post-processor must wrap an existing Emscripten implementation, not invent or replace it:

```js
var __godotMinigameOriginalCommitFrame = _emscripten_webgl_commit_frame;
_emscripten_webgl_commit_frame = function () {
    var result = __godotMinigameOriginalCommitFrame();
    var context = typeof GL !== "undefined" && GL.currentContext && GL.currentContext.GLctx;
    if (context && typeof context.flush === "function") context.flush();
    if (context && typeof context.commit === "function") context.commit();
    return result;
};
```

Never emit:

```js
var _emscripten_webgl_commit_frame = function () {};
```

## Build And Package

From the adapted Godot source root:

```powershell
& C:\global\emsdk\python\3.13.3_64bit\Scripts\scons.exe `
  platform=web target=template_release threads=no `
  wasm_simd=no use_wx_glx=yes

cmd /c compress_wasm.bat
```

If auto-detection cannot find the exact vendor library, pass `wx_glx_lib=<absolute-path>` explicitly. Package the processed `bin/.web_zip/godot.js` and `godot.wasm.br`, not the raw linker outputs.

## Required Validation

Run source-level tests in the adapted Godot checkout:

```powershell
node platform/web/js/tests/test_wechat_glx_runtime.js
node platform/web/js/tests/test_godot_process_commit_frame.js
```

Run runtime-shell and package tests from the repository root:

```powershell
node adapter/tests/test_min_runtime_loader.js
node adapter/tests/test_godot_process_commit_frame.js
node adapter/tests/test_template_glx_config.js <unpacked-template>
node adapter/scripts/validate_template_runtime.js <unpacked-template> wxglx
```

Verify both startup paths:

- default supported device requests `wxwebgl2` and pins `true`
- unsupported device or explicit disable requests `webgl2` and pins `false`
- pinned WXGLX cannot silently return a standard context
- generated `godot.js` contains `__godotMinigameOriginalCommitFrame`
- generated `godot.js` does not contain the empty commit-frame function
- the GLX and standard paths both reach scene rendering on real devices

## Failure Diagnosis

- Black screen only on iOS or non-GLX devices: inspect the standard frame-commit wrapper first.
- Loader renders but engine fails to create a context: verify loader/engine mode pinning and import order.
- `The loader selected WXGLX, but the native bindings are missing`: the loader was paired with a non-GLX engine or the vendor library was not linked/exported.
- `Canvas context mode mismatch`: a context was created before policy was pinned, or loading and engine canvases selected different modes.
- `resizeOffscreenFramebuffer is not a function`: an offscreen-framebuffer call is still unguarded.
- GLX starts and then stalls: verify context initialization timing, command-buffer flush registration, timestamp-query guards, and exact Emscripten/library version matching.
