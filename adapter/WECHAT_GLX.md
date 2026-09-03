# Godot 4.5.2 微信小游戏 EmscriptenGLX 适配

本文说明仓库中的 GLX 适配原理、关键源码改动和正式产物流程。详细维护规则由项目 Skill `.agent/skills/godot-wechat-minigame-adapter/` 记录。

## 状态与版本

前一版候选已通过 Android 真机验证：Godot Renderer 正常启动、项目场景可见且能够连续渲染。本页对应的新 `.tpz` 已通过干净构建和静态校验，仍需做一次同包真机冒烟验证。四条 GLX 初始化日志本身不算通过，验收仍以 Renderer、场景和稳定帧为准。

| 项目 | 固定值 |
| --- | --- |
| Godot | `4.5.2-stable` / `6ce3de25aa58466e14ef354703ba8d9791a417da` |
| Emscripten | `4.0.10` |
| EmscriptenGLX | `0.1.11` |
| GLX 对齐来源 | `citizenll/godot@3dec2ca498f7b9e1ce07b33f5fbe08741a1429e5` |
| Brotli | `1.2.0` |
| 构建开关 | `wechat_glx=yes` |

静态库来源、下载地址和 SHA-256 见 [`thirdparty/wechat-glx/SOURCE.md`](thirdparty/wechat-glx/SOURCE.md)。

## 为什么需要 Godot 适配

微信 GLX 不是普通 WebGL context 名称替换。它把 GL 调用交给微信原生 command buffer，并由微信 frame callback 驱动提交。微信官方文档给出了静态库、链接参数和 context 初始化，但没有覆盖 Godot GLES3 Renderer 的完整运行模型。

旧实现已经能创建 `wxwebgl2`、调用 `_glxInit*` 并打印初始化日志，但 Godot 仍执行普通 WebGL 的 timestamp Query、offscreen framebuffer、explicit swap 和 `emscripten_webgl_commit_frame()` 路径。GLX command buffer、异常模型和帧所有权因此不一致，Renderer 可能在 banner 或首帧前退出。

本次修复按 citizenll 的同一组稳定化改动整体回移。没有做单变量 A/B，因此不能把根因严谨地归结为某一行；Query、null-write、exceptions、offscreen、swap 和 longjmp 是同一条兼容链。

## 关键源码改动

| 文件 | 改动 | 原因 |
| --- | --- | --- |
| `platform/web/detect.py` | 新增 `wechat_glx`；锁定 Emscripten 4.0.10；链接 GLX；启用 C++ exceptions；设置 `CHECK_NULL_WRITES=0`、Emscripten longjmp；GLX 下不启用 offscreen framebuffer | 与官方静态库 ABI 和 citizenll 构建模型一致 |
| `drivers/gles3/storage/utilities.cpp` | GLX 下编译掉 GLES timestamp Query 的创建、删除、写入和读取 | GLX 0.1.11 不提供 Godot 这条 Query 链所需的完整语义 |
| `platform/web/display_server_web.cpp` | GLX 下关闭 explicit swap control，并跳过 `emscripten_webgl_commit_frame()` | 帧提交由 GLX command buffer 和微信 callback 所有 |
| `platform/web/js/libs/library_godot_display.js` | resize 前检查 `GL.resizeOffscreenFramebuffer` 是否存在 | GLX 构建不创建普通 offscreen framebuffer 实现 |
| `platform/web/js/patches/patch_em_gl.js` | 映射 `webgl*` 到 `wxwebgl*`；固定 Loader 选择的模式；注册 native context；更新 context id | 让 Emscripten GL context 与微信 GLX native binding 使用同一个上下文 |
| `godot_process.js` | 补齐生成 JS 的 invoke imports、RuntimeError/微信宿主兼容和 frame callback 包装 | 处理 Emscripten 4.0.10 生成物与小游戏 JS 宿主之间的确定性差异 |

`patch_em_gl.js` 与 citizenll 对齐源逐字节一致。runtime bridge 只从这个 Godot post-js 进入生成物；`godot_process.js` 不再动态注入备用 bridge，也不注入 Query bypass 或诊断代码。

Loader 先决定普通 WebGL 或 GLX 模式，并写入 `GameGlobal.__godotMinigameWXGLXEnabled`。引擎必须遵守该固定选择；已选择 GLX 时创建失败会直接报错，不能在同一 canvas 上静默回退到普通 WebGL。

## 包体与 C++ 异常（2026-08 实测）

同一份 `templates/configs/wechat_2d.py` 裁切配置下，Brotli `godot.wasm.br` 的实测体积：

| 构建 | wasm.br | 函数数 | 说明 |
| --- | --- | --- | --- |
| 非 GLX | ≈ 4.85 MiB | — | 异常默认关闭（Godot `disable_exceptions=yes` 默认） |
| GLX + 异常开启 | ≈ 6.05 MiB | 75,064 | 默认产物 |
| GLX + 异常关闭 | ≈ 4.91 MiB | 60,141 | 仓库早期 demo 4.6.1_glx 同状态 |

结论：

- **GLX 静态库本身只增加约 67 KB**；6.05 MiB 与 4.85 MiB 之间约 1.14 MiB 的差异几乎全部来自 C++ 异常支持（`-fexceptions` + Emscripten 异常胶水）。
- **为什么 GLX 需要异常**：`libemscriptenglx.a` 内部代码会 `throw`（`__cxa_throw`/`__cxa_allocate_exception` 符号已用 `llvm-nm` 验证）。异常关闭时若 GLX 库走到 throw 路径会直接 abort；正常渲染路径不抛异常时可运行。
- **开关已实现**：`platform/web/detect.py` 提供 `wechat_glx_exceptions=yes|no`（默认 `yes`，子模块 `08024e25`）。`no` 时保持 `disable_exceptions=yes` 并跳过 `-fexceptions`。
  - 统一入口：`python ci/package.py --template 4.5.2 --variant glx --profile 2d --exceptions enabled|disabled`
  - CI：`build-wechat-glx.yml` 的 `profile`（裁切清单，`templates/configs/*.py`）与 `exceptions` 输入
  - 选择建议：**测试阶段用 `enabled`**（GLX 库抛异常有兜底）；**游戏完成发布时可用 `disabled`** 省约 1.14 MiB，前提是接受 GLX 库异常路径直接 abort 的风险。
- 上游 godothub 的 4.6.2 GLX 模板（含 3D）为 7.52 MiB，与本仓库 4.5.2 裁切版 6.05 MiB 同属"异常开启"产物族；正式发布物不存在 5.8 MiB 级别的 4.6 GLX 模板。

## 源码与产物边界

长期维护源位于：

- `adapter/patches/optional/003-wechat-glx.patch`
- `adapter/sources/optional/platform/web/js/patches/patch_em_gl.js`
- `adapter/sources/godot_process.js`
- `adapter/thirdparty/wechat-glx/`
- `adapter/assets/min-runtime/`

`godot/` 是应用后并用于编译的子仓库。`dist/` 中的 `engine/godot.js` 和 `godot.wasm.br` 是生成产物，不能作为源码修复入口。

## 应用与构建

从官方固定基线重放适配：

```powershell
git submodule update --init godot
python adapter\scripts\apply_godot_patchset.py --include-optional wechat-glx
```

工具箱导出还需要 `export-api` 时，应在同一次调用中添加该 optional feature。

激活 Emscripten `4.0.10` 后，在 `godot/` 中构建：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes
cmd /c compress_wasm.bat
```

Linux/macOS 使用 `./compress_wasm.sh`。压缩脚本生成 `bin/.web_zip/godot.wasm.br`，并对 `godot.js` 执行幂等后处理；不能省略。

## 正式模板与 TPZ

Godot 源码必须先提交并保持干净，随后在仓库根目录执行：

```powershell
统一入口（推荐）：

```powershell
python ci\package.py --template 4.5.2 --variant glx --profile 2d --exceptions enabled --revision 1
```

打包器默认先执行一次干净 SCons 构建和 Brotli/JS 后处理，然后执行以下检查和操作：

1. 校验 Godot HEAD 精确匹配 manifest、维护源同步、GLX 静态库和 bridge 哈希。
2. 检查 SCons 参数、生成 JS 语法、runtime methods、GLX bindings、bridge 数量和禁止的诊断/shim。
3. 解压 Brotli WASM，校验 9 个 GLX exports、batch callbacks，并拒绝 Query/commit-frame imports。
4. 从维护源同步 Loader/SDK，从本次构建目录同步 `godot.js` 和 `godot.wasm.br`。
5. 更新 `BUILD_INFO.md`。
6. 全新创建确定性的 `.tpz`，不增量更新旧 ZIP。
7. 回读归档，确认无重复条目、无多余顶级目录，并与解包模板逐文件一致。

产物链固定为：

```text
Godot source
  -> SCons
  -> godot.js + godot.wasm
  -> godot_process.js + Brotli
  -> templates/4.5.2/（打包基底，格式文件）
  -> dist/minigame4.5.2-glx-2d-r{N}/（完整模板）
  -> dist/minigame4.5.2-glx-2d-r{N}.tpz（分发包）
```

模板只分发后处理后的 `engine/godot.js` 和 `engine/godot.wasm.br`，不分发 `godot.raw.js` 或未压缩 WASM。

## 自动化验证

```powershell
node adapter\tests\test_godot_process_glx.js
node adapter\sources\optional\platform\web\js\tests\test_wechat_glx_runtime.js
node --check godot\bin\.web_zip\godot.js
```

两个测试职责不同：第一个保护生成 JS 后处理及幂等性；第二个保护 context 选择、模式固定和 native 初始化。测试源复制到 Godot 树中是 patchset 的组成部分，不是另一套实现。

真机最终验收必须同时满足：

- 出现 GLX 初始化日志。
- 出现 Godot Renderer banner。
- 项目场景实际可见。
- 连续帧稳定，无首帧退出或闪退。

开发者工具中的普通 WebGL fallback 只能检查基础兼容，不能替代 GLX 真机结果。重新构建后二进制哈希会变化，发布前应使用最终 `.tpz` 做一次同包真机冒烟验证。

## 参考

- [微信 EmscriptenGLX 原生引擎接入](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-emscriptenglx-native.html)
- [citizenll GLX 对齐提交](https://github.com/citizenll/godot/tree/3dec2ca498f7b9e1ce07b33f5fbe08741a1429e5)
- [`patches/README.md`](patches/README.md)
- [`references/validation-checklist.md`](references/validation-checklist.md)
