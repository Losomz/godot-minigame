# Build Information

- Godot: `4.5.2-stable` (`6ce3de25aa58466e14ef354703ba8d9791a417da`)
- Godot Fork commit: `97fc04e67a6b583491230f2a24003415cedad078`
- GLX alignment source: `citizenll/godot@3dec2ca498f7b9e1ce07b33f5fbe08741a1429e5` backported to Godot 4.5.2
- Emscripten: `4.0.10` (`emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 4.0.10 (b7dc6e5747465580df5984e723b9d1f10d8e804b)`)
- WeChat EmscriptenGLX: `0.1.11`
- Build command: `scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes`
- Post-process: `godot_process.js`
- Compression: `brotli 1.2.0`
- Device validation: predecessor candidate passed Android real-device rendering; this rebuilt archive requires a same-package smoke test

## Artifact SHA256

- `engine/godot.js`: `CE12E428B4227F6FA24B797123C08F5D1AB81E4E1328DCABD2A53569370A35B9`
- `engine/godot.wasm.br`: `9FC85A53E79E4A44FE40F4843DDC82BEC9A30755A0063C46F4E3C642DC515748`
- decompressed `engine/godot.wasm`: `D687E5303E62C0EF455F42EB017353B8870DA00CC794228C1F059CF4BB3EB3D4`
- `godot-loader.js`: `0E6BDE33B9A63D3F452209838F0EF110A33890DF8BC97FDEDF25728BF00C03A4`
- `engine/godot-sdk.js`: `26D3B9D04B419BB21191978FC0AD8D6D251EA19AAD2463AEE825751C864FA25D`
