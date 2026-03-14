# Minimal SDK Runtime

Use this reference when removing the external `godot-minigame-sdk` dependency while keeping the old global interface names unchanged.

## What Current Godot Actually Uses

The current Godot-side runtime depends on only these pieces from the old SDK:

- `GODOTSDK.audio.WEBAudio.audioContext`
  - used by `platform/web/js/libs/library_godot_audio.js`
- `fsUtils.localFetch`
  - used by `platform/web/js/engine/preloader.js`
- `__globalAdapter.onKeyboardComplete`
- `__globalAdapter.hideKeyboard`
- `__globalAdapter.offKeyboardInput`
- `__globalAdapter.offKeyboardConfirm`
- `__globalAdapter.offKeyboardComplete`
  - used by `platform/web/js/libs/library_godot_input.js`
- `GameGlobal.GODOTSDK` as the host object that `library_godot_memfs.js` augments with:
  - `releasePck`
  - `getWxPath`
  - `getGodotPath`
- `GameGlobal.nowPolyfill`
  - required because `godot_process.js` rewrites generated timing code to `nowPolyfill`

Keep the following compatibility entries even though the current Godot web libs do not call them directly:

- `GODOTSDK.startGame`
- `GODOTSDK.copy_to_fs`
- `GODOTSDK.load_pack`
- `GameGlobal.GodotLoader`

These are part of the surrounding minigame bootstrap contract and allow old startup scripts to keep working.

## Files Bundled In This Skill

- `assets/min-runtime/godot-sdk.js`
  - minimal compatibility shell
- `assets/min-runtime/godot-loader.js`
  - independent loader script

## Preserved Global Names

The minimal runtime intentionally preserves these names:

- `GameGlobal.GODOTSDK`
- `window.fsUtils`
- `globalThis.fsUtils`
- `window.__globalAdapter`
- `globalThis.__globalAdapter`
- `GameGlobal.nowPolyfill`
- `GameGlobal.GodotLoader`

## What The Minimal `godot-sdk.js` Contains

- `nowPolyfill`
- `fsUtils.localFetch`
- `fsUtils.loadSubpackage`
- keyboard-only `__globalAdapter` methods
- `GODOTSDK.audio.WEBAudio.audioContext`
- `GODOTSDK.startGame`
- `GODOTSDK.copy_to_fs`
- `GODOTSDK.load_pack`
- `WXWebAssembly -> WebAssembly` override

## What Was Deliberately Removed

Do not pull these from the old `godot-minigame-sdk` unless a future real usage search proves they are needed:

- the full audio subsystem API from `audio/`
- general download/copy/read/write helper families in `fsUtils`
- multi-platform wrappers for non-WeChat targets
- unrelated polyfills and helper libraries
- test-only APIs such as `XMLHttpRequest` and `isCacheableFile` used in `platform/web/js/tests/audio-unity.js`

The rule is simple:

- trust runtime usage in `platform/web` and `modules`
- ignore old SDK surface area that only existed for other engines, other platforms, or tests

## Recommended Load Order

Load these before the generated Godot runtime:

1. `godot-sdk.js`
2. `godot-loader.js`
3. generated `godot.js`

That order ensures:

- `nowPolyfill` exists before patched `godot.js` runs
- `fsUtils.localFetch` exists before `preloader.js` runs
- `GODOTSDK.audio.WEBAudio.audioContext` exists before `library_godot_audio.js` initializes
- `GameGlobal.GODOTSDK` exists before `library_godot_memfs.js` augments it

## Copy Strategy

If you are vendoring these files into a fresh target repo or downstream minigame host project, copy both files together and keep them versioned alongside the port. Do not keep them as an external machine-local dependency.
