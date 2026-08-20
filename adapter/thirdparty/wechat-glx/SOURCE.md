# WeChat EmscriptenGLX static library

## What this is

`libemscriptenglx.a` is the official WeChat Mini Game EmscriptenGLX (GLX)
static library, linked into the Godot web build to enable the high-performance
native GL rendering path inside WeChat Mini Games (iOS 高性能+ mode and
Android, base library >= 3.8.12).

## Provenance

- Source: WeChat official download endpoint (documented in
  [EmscriptenGLX 原生引擎接入](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-emscriptenglx-native.html))
- Download URL:
  `https://game.weixin.qq.com/cgi-bin/gamewxagwasmsplitwap/getunityplugininfo?download=1&biz_id=1&version=0.1.11`
- Package: `libs_emscriptenglx.zip`, GLX version **0.1.11**, built **2025.12.05**
  (see `version.txt`)
- File kept from the package: `libemscriptenglx_4.0.10.a` (the emsdk **4.0.10**
  variant), renamed to `libemscriptenglx.a` so the documented `-lemscriptenglx`
  linker flag resolves it directly.
- SHA256 (`libemscriptenglx.a`):
  `C70B6255285AA4E5B987F18CF24D59D56227C67D44D60B734755F33F6A8AB33A`

## Why this exact file

- The repo toolchain is pinned to Emscripten **4.0.10**; the official support
  list for GLX v0.1.11 is emsdk 3.1.17 / 3.1.74 / 4.0.10. The `4.0.10` build is
  the exact ABI match.
- GLX v0.1.11 includes the native-engine integration support and the
  `glReadPixels` rendering fix (official changelog 2025.12.5).
- The `version=latest` package (GLX v0.1.12, 2026.03.23) currently ships only a
  `libemscriptenglx_3.1.51.a` build, which carries newer libc++ ABI tags and is
  not the matching toolchain variant for emsdk 4.0.10, so it is not used.

## Other variants (not stored in the repo)

The v0.1.11 package also contains `libemscriptenglx_3.1.17.a` and
`libemscriptenglx_3.1.74.a` for those emsdk versions. Re-download the package
from the URL above if a different toolchain variant is ever needed.

## Link flags (per official docs)

```text
-L<libPath>
-lemscriptenglx
-s EXPORTED_RUNTIME_METHODS=ccall,cwrap,stringToUTF8,lengthBytesUTF8
-s ERROR_ON_UNDEFINED_SYMBOLS=0
```
