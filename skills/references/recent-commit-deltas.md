# Recent Commit Deltas

Use this file after the baseline port is working. These are not the whole WeChat migration. They are the latest compatibility deltas that must be replayed if the target branch still misses them.

## Which commits to use

For migration purposes, use the latest **minigame-specific non-merge commits**. Ignore pure upstream merge commits and plain version bumps when extracting runtime behavior.

## Delta 1: Fetch chunk copy offset

- Commit: `9f1e9f8ad6`
- Subject: `web/minigame: fix fetch chunk copy offset corruption on large responses`
- Owned file:
  - `platform/web/js/libs/library_godot_fetch.js`

### What changed

- `godot_js_fetch_read_chunk` now tracks a cumulative `write_offset`.
- Each chunk copy writes into `p_buf + write_offset` instead of repeatedly writing to the same base pointer.

### Why it matters

Without this fix, large HTTP responses can overwrite previously copied bytes, so the reconstructed body is corrupted even though chunk callbacks succeed.

### Replay check

```powershell
rg -n "write_offset|p_buf \\+ write_offset|chunks\\[0\\] = chunk.slice" platform/web/js/libs/library_godot_fetch.js
```

### Acceptance signal

- Large downloads are byte-correct.
- Small downloads still work.

## Delta 2: Disable WASM SIMD by default

- Commit: `6e53a06012`
- Subject: `web: disable wasm simd by default for iOS minigame compatibility`
- Owned files:
  - `platform/web/detect.py`
  - `modules/raycast/SCsub`

### What changed

- `wasm_simd` default changed from enabled to disabled.
- `-msimd128` is only appended when `env["wasm_simd"]` is true.

### Why it matters

Older iOS WebKit inside WeChat Mini Game can fail badly with SIMD-enabled builds. The safe default is off, with an opt-in flag for controlled testing.

### Replay check

```powershell
rg -n "wasm_simd|msimd128" platform/web/detect.py modules/raycast/SCsub
```

### Acceptance signal

- Default build works on iOS WeChat.
- SIMD can still be re-enabled intentionally for experiments.

## Delta 3: Audio, display, and input cleanup bundle

- Commit: `bbdf3b0c80`
- Subject: `fix:修复音频和输入bug&分辨率`
- Owned files:
  - `platform/web/SCsub`
  - `platform/web/js/libs/library_godot_audio.js`
  - `platform/web/js/libs/library_godot_display.js`
  - `platform/web/js/libs/library_godot_input.js`
  - `platform/web/js/libs/lib.wx.api.d.ts`

### What changed

- `platform/web/SCsub` switched the active audio entry back to `library_godot_audio.js`.
- `library_godot_audio.js` gained reusable audio-context pooling and stronger cleanup:
  - `MAX_POOL_SIZE`
  - `resetContext`
  - `destroyContext`
  - `cleanupPlayback`
  - better `onStop` and `onError` cleanup
  - explicit `loop` and `startTime` handling
- `library_godot_display.js` now prefers `wx.getWindowInfo()` for pixel ratio and window size.
- `library_godot_input.js` now bridges WeChat keyboard input by replaying key events against Godot's IME path instead of relying on browser DOM behavior.
- `lib.wx.api.d.ts` gained `WindowInfo` and `getWindowInfo()` typing.

### Why it matters

This bundle moves the current runtime toward stable repeatable behavior on real devices:

- fewer audio leaks and stuck playbacks
- correct high-DPI sizing
- more reliable text replacement and confirm/complete flows

### Replay check

```powershell
rg -n "library_godot_audio|contextPool|cleanupPlayback|getWindowInfo|showKeyboard|WindowInfo" platform/web/SCsub platform/web/js/libs
```

### Acceptance signal

- repeated sound playback remains stable
- screen size and DPI are correct on WeChat devices
- keyboard input replaces text cleanly and confirm/complete behave

## Replay Order

Apply these in this order:

1. Delta 3 if your runtime still looks like the older audio/input/display path
2. Delta 2 if your build still enables SIMD by default
3. Delta 1 if your fetch bridge lacks `write_offset`

That order keeps runtime wiring stable before you debug network corruption.

## Non-deltas to ignore

Do not treat these as migration deltas:

- upstream merge commits
- plain version bumps such as `a16e481cf4`

They may be relevant to branch hygiene, but they do not explain the WeChat runtime behavior.
