# Validation Checklist

Run these checks from the target repo root. Use them after each module and again before declaring the port complete.

## 1. Static Anchors

Verify the expected WeChat hooks are present:

```powershell
rg -n "WXMEMFS|enableChunked|onChunkReceived|getWindowInfo|showKeyboard|wx.exitMiniProgram|wasm_simd" platform/web modules version.py
```

If you are replacing the old external SDK, also verify the bootstrap globals:

```powershell
rg -n "GODOTSDK\\.|fsUtils\\.|__globalAdapter\\.|nowPolyfill|GodotLoader" platform/web modules
```

## 2. Build Smoke Test

Build a web template with conservative defaults:

```powershell
scons platform=web target=template_release threads=no wasm_simd=no
```

If the repo uses a different target or helper script, keep the same intent:

- no browser-only threading assumptions
- no default SIMD
- generate the standard web export bundle before post-processing

## 3. Packaging Smoke Test

Run the bundled scripts with the repo root as the current working directory:

```powershell
node <skill-dir>\scripts\godot_process.js
cmd /c <skill-dir>\scripts\compress_wasm.bat
```

Then confirm:

```powershell
Test-Path .\bin\.web_zip\godot.js
Test-Path .\bin\.web_zip\godot.wasm
Test-Path .\bin\.web_zip\godot.wasm.br
```

## 4. Generated JS Sanity

Check the final generated JS for the expected runtime hooks:

```powershell
rg -n "FS.mount\\(WXMEMFS|nowPolyfill|wx.exitMiniProgram|wasm\\.br" .\bin\.web_zip\godot.js platform/web/js/engine
```

## 5. File System Smoke Test

In WeChat DevTools or through a minimal Godot test scene:

- write a file under `user://`
- close it
- reopen it immediately and read it back
- restart the app and read it again

Failure modes to watch:

- write succeeds but immediate read returns stale data
- file exists only until restart
- `IDBFS` is still mounted instead of `WXMEMFS`

## 6. Network Smoke Test

Use a payload large enough to require multiple chunk callbacks.

Expected results:

- headers parse correctly
- body length matches expectation
- downloaded bytes are not corrupted

Failure mode to watch:

- body corruption caused by missing `write_offset` in `godot_js_fetch_read_chunk`

## 7. Audio Smoke Test

Test all three paths:

- play once
- stop early
- replay or loop

Expected results:

- no crash on stop
- no duplicate or stuck playback objects
- repeated reuse does not exhaust the context pool

## 8. Display And Input Smoke Test

Check on an actual WeChat device or DevTools simulation:

- image is sharp on high-DPI screens
- window size matches expected full-screen area
- keyboard can type, replace, confirm, and dismiss text cleanly

Failure modes to watch:

- blurry rendering from browser `devicePixelRatio` fallback
- duplicate characters from bad IME replay
- wrong fullscreen size from browser `window.innerWidth` assumptions

## 9. Exit And Lifecycle Smoke Test

Confirm:

- runtime exit follows `wx.exitMiniProgram()`
- JS eval path stays disabled and does not crash

## 10. Completion Bar

Call the migration complete only when all are true:

- build passes
- post-processing and compression produce the expected files
- WXMEMFS persistence works
- large HTTP bodies are correct
- audio replay is stable
- display and keyboard work
- exit path uses WeChat lifecycle
