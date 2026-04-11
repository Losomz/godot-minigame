# Recent Commit Deltas

These deltas are already baked into the bundled `godot-4.6.2-rc-a16e481cf4` core package. Do not re-apply them when the target repo is exactly on the supported base and you used `scripts/apply_godot_patchset.py`.

Use this file only when:

- the target repo is newer than `a16e481cf424f8e39dc2cdea1a6bdc1e309acdc1`
- you are replaying the public bundle forward onto a nearby upstream commit
- the bundled patch applies with manual conflict resolution and you need to verify that specific regression fixes survived

## Delta 1: Fetch chunk copy offset

- Commit: `9f1e9f8ad6`
- Subject: `web/minigame: fix fetch chunk copy offset corruption on large responses`
- Current package status: included in shipped `library_godot_fetch.js`

### Why it matters

Without cumulative `write_offset`, large HTTP bodies overwrite earlier copied bytes and the final payload is corrupted.

### Replay check

```powershell
rg -n "write_offset|p_buf \\+ write_offset|chunks\\[0\\] = chunk.slice" platform/web/js/libs/library_godot_fetch.js
```

## Delta 2: Disable WASM SIMD by default

- Commit: `6e53a06012`
- Subject: `web: disable wasm simd by default for iOS minigame compatibility`
- Current package status: included in core patch

### Why it matters

Older iOS WebKit in WeChat Mini Game can fail with SIMD-enabled defaults. The safe default is off.

### Replay check

```powershell
rg -n "wasm_simd|msimd128" platform/web/detect.py modules/raycast/SCsub
```

## Delta 3: Audio, display, and input cleanup

- Commit: `bbdf3b0c80`
- Subject: `fix:修复音频和输入bug&分辨率`
- Current package status: included in shipped source files plus core patch

### Why it matters

This bundle is what makes the public package stable on real devices:

- pooled/reused audio contexts
- stronger playback cleanup
- `wx.getWindowInfo()` sizing
- WeChat keyboard bridge

### Replay check

```powershell
rg -n "contextPool|cleanupPlayback|getWindowInfo|showKeyboard" platform/web/js/libs
```

## Forward-Port Rule

If the target repo is newer than the supported base:

1. apply the public bundle as far as it will go
2. resolve conflicts module by module
3. re-run the replay checks above
4. validate runtime behavior, not just anchors
