# Godot 4.5.2 微信小游戏 EmscriptenGLX 适配全流程

本文记录 Godot `4.5.2-stable` 接入微信小游戏 EmscriptenGLX 的完整过程，包括依赖来源、Godot 构建补丁、生成 JS 后处理、运行时上下文桥接、模板打包、真机验收和本次问题定位复盘。

本文对应的实现源位于 `adapter/`。`godot/` 子仓库是补丁应用和构建现场，`templates/4.5.2/minigame4.5.2_glx/` 是最终模板产物。后续维护应修改 `adapter/` 中的源文件，再重新应用或同步到 Godot 工作树，不应把已生成的 `engine/godot.js` 当作长期维护源。

## 1. 已验证结论

当前适配已经在 Android 真机上确认启用 EmscriptenGLX，成功日志为：

```text
Wechat GLX Lib: 0.1.11
当前为 EmscriptenGLX 渲染方案
registerAfterDoFrame register
完成BufferData逻辑初始化
```

这四条日志分别证明：

1. 官方 `libemscriptenglx` 已进入并运行。
2. 引擎创建的是带 `emscriptenGLX` 数据的微信原生 GL 上下文。
3. GLX 帧结束回调已经注册。
4. BufferData 和 GL 状态初始化已经完成。

仅满足下面任意一项都不能单独证明 GLX 已启用：

- `wx.env.isSupportEmscriptenGLX === true`
- WASM 中存在 `_glxInit` 等导出
- loader 请求过 `wxwebgl2`
- 控制台出现 `Wechat GLX Lib` 版本信息

如果日志显示：

```text
当前为普通 WebGL/WebGL2 渲染方案
```

说明 bridge 和静态库已经运行，但传给 `_glxInit` 的值是 `false`，当前引擎上下文仍是普通 WebGL。开发者工具中的这种回退属于本项目实测的预期行为，最终必须以 Android 或满足条件的 iOS 真机日志为准。

## 2. 版本矩阵

| 项目 | 固定或验证值 | 说明 |
| --- | --- | --- |
| Godot | `4.5.2-stable` | 官方稳定版 |
| Godot 基线提交 | `6ce3de25aa58466e14ef354703ba8d9791a417da` | 补丁只保证此基线可重复应用 |
| Emscripten | `4.0.10` | 与静态库 ABI 精确匹配 |
| EmscriptenGLX | `0.1.11` | 构建时间 `2025.12.05 16:38:56` |
| Brotli | `1.2.0` | 当前模板构建记录 |
| 微信基础库 | `>= 3.8.12` | 真机验证使用 `3.17.1` |
| Android | 支持 | 本次已真机验证 |
| iOS | iOS 14+，高性能+ | 还需要满足微信客户端和后台能力要求 |
| 微信客户端 | 高性能+场景建议 `>= 8.0.45` | 以微信官方最新要求为准 |
| 开发者工具 | 普通 WebGL 回退 | 不能代替 GLX 真机验收 |

模板中的 `game.json` 必须保留：

```json
{
  "iOSHighPerformance": true,
  "iOSHighPerformance+": true
}
```

iOS 还需要在小游戏后台开通高性能+能力。只修改 `game.json` 而没有开通后台能力，不会自动获得 GLX 上下文。

## 3. GLX 与通用微信适配的边界

Godot 子仓库的 dirty 工作树包含完整微信小游戏适配，不能把其中所有改动都归为 GLX。

### 3.1 通用微信小游戏适配

下面这些内容在不启用 GLX 时也需要：

- WXMEMFS 和微信文件系统映射
- `wx.request` 网络适配
- 微信原生长音频和 WebAudio 兼容
- `wx.getWindowInfo()`、像素比和显示适配
- 微信键盘和输入事件
- `wx.exitMiniProgram()` 生命周期
- `.wasm.br` 本地加载
- `nowPolyfill`
- JavaScript eval 限制处理
- `WASM_BIGINT`、SIMD 和旧 WebKit 兼容调整
- `godot-sdk.js`、`godot-loader.js` 和 `weapp-adapter.js`

这些内容主要来自核心补丁和 `adapter/sources/platform/web/`，不是 `003-wechat-glx.patch` 单独提供的功能。

### 3.2 GLX 专属增量

GLX 增量包括：

- 官方 `libemscriptenglx.a`
- `wechat_glx` SCons 构建选项
- Emscripten `4.0.10` 精确版本检查
- GLX 链接参数和异常配置
- `SUPPORT_LONGJMP='emscripten'`
- `_glxInit`、`_glxInitBufferDataAndGlState`、`_glxUpdateContextId` 和 `_glxCommandBufferFlush`
- `wxwebgl`、`wxwebgl2` 上下文映射
- WXGLX runtime bridge
- invoke import 补全
- `emscripten_webgl_commit_frame` shim
- `registerAfterDoFrame` callback wrapper
- loader 与引擎上下文模式固定和一致性检查
- GLX 后处理自动化测试

## 4. 三层结构

整个适配分为三层。

### 4.1 源码与补丁层

主仓库中的源文件负责可重复安装：

| 主仓库源文件 | 作用 | 应用后的目标 |
| --- | --- | --- |
| `adapter/thirdparty/wechat-glx/` | 官方静态库、版本和来源 | `godot/thirdparty/wechat-glx/` |
| `adapter/patches/optional/003-wechat-glx.patch` | Web 构建选项和链接配置 | `godot/platform/web/detect.py` |
| `adapter/patches/manifest.json` | 注册 `wechat-glx` 可选功能 | 补丁安装器读取 |
| `adapter/scripts/apply_godot_patchset.py` | 复制静态库和应用补丁 | 目标 Godot 工作树 |
| `adapter/sources/godot_process.js` | 生成 JS 后处理和 runtime bridge | `godot/godot_process.js` |
| `adapter/sources/compress_wasm.*` | Brotli 压缩并调用后处理 | Godot 根目录 |
| `adapter/tests/test_godot_process_glx.js` | bridge 自动化测试 | 从主仓库直接运行 |

`adapter/sources/godot_process.js` 是最终后处理逻辑的源文件。此次保留的构建现场中，`godot/godot_process.js` 是较早的副本，只包含 invoke、commit-frame 等修复，不包含最后补齐的 runtime bridge。它可以用于理解排查过程，但不能作为后续重建的源。

### 4.2 Godot 构建与生成层

Godot 构建产生：

```text
godot/bin/.web_zip/godot.js
godot/bin/.web_zip/godot.raw.js
godot/bin/.web_zip/godot.wasm
godot/bin/.web_zip/godot.wasm.br
```

其中 `bin/` 被 Godot `.gitignore` 忽略，普通 `git status` 不会显示这些生成物。子仓库中的 `libemscriptenglx.a` 也会被 `*.a` 规则忽略。因此不能通过“Git 状态里没有文件”判断构建产物或静态库不存在。

### 4.3 模板与分发层

最终小游戏模板位于：

```text
templates/4.5.2/minigame4.5.2_glx/
```

分发归档为：

```text
templates/4.5.2/minigame4.5.2_glx.tpz
```

模板只分发后处理后的 `engine/godot.js` 和 Brotli 压缩的 `engine/godot.wasm.br`，不分发 `godot.raw.js` 或未压缩的 `godot.wasm`。

## 5. 官方静态库来源

依赖来源记录在 [`thirdparty/wechat-glx/SOURCE.md`](thirdparty/wechat-glx/SOURCE.md)。固定下载地址为：

```text
https://game.weixin.qq.com/cgi-bin/gamewxagwasmsplitwap/getunityplugininfo?download=1&biz_id=1&version=0.1.11
```

`0.1.11` 包提供：

```text
libemscriptenglx_3.1.17.a
libemscriptenglx_3.1.74.a
libemscriptenglx_4.0.10.a
version.txt
```

仓库选择 `libemscriptenglx_4.0.10.a`，并重命名为：

```text
libemscriptenglx.a
```

这样链接参数 `-lemscriptenglx` 可以直接解析该文件。

静态库 SHA-256 必须为：

```text
C70B6255285AA4E5B987F18CF24D59D56227C67D44D60B734755F33F6A8AB33A
```

验证命令：

```powershell
certutil -hashfile adapter\thirdparty\wechat-glx\libemscriptenglx.a SHA256
```

或：

```bash
sha256sum adapter/thirdparty/wechat-glx/libemscriptenglx.a
```

不要直接把官方 `version=latest` 下载结果覆盖进仓库。截至本次适配时，`0.1.12` latest 包只提供 `3.1.51` 变体，与当前 Emscripten `4.0.10` 工具链不匹配。升级 GLX 时必须同时确认版本、工具链变体、ABI 和真机回归结果。

## 6. 环境准备

需要以下工具：

- Git
- Python
- SCons
- Emscripten `4.0.10`
- Node.js
- Brotli CLI
- 微信开发者工具
- 可上传体验版并查看日志的小游戏 AppID

检查 Emscripten：

```powershell
emcc --version
em++ --version
```

两者必须指向当前激活的 Emscripten `4.0.10`。`wechat_glx=yes` 会主动拒绝其他版本。

Windows 下建议把独立 Godot 源码放到短路径，例如：

```text
C:\g
```

Godot Web 最终链接命令很长，深层目录容易超过 `cmd.exe` 的命令行长度限制。

## 7. 从干净基线安装补丁

### 7.1 初始化并确认基线

在主仓库根目录运行：

```powershell
git submodule update --init godot
git -C godot rev-parse HEAD
git -C godot status --short
```

预期 HEAD 必须是：

```text
6ce3de25aa58466e14ef354703ba8d9791a417da
```

首次应用前，Godot 工作树必须干净。安装脚本默认拒绝脏工作树和错误基线，这是为了避免补丁部分应用后留下不可复现状态。

### 7.2 一次性安装通用适配和 GLX

```powershell
python adapter\scripts\apply_godot_patchset.py --include-optional wechat-glx
```

如果还需要工具箱导出 API，必须在同一次调用中加入：

```powershell
python adapter\scripts\apply_godot_patchset.py `
  --include-optional export-api `
  --include-optional wechat-glx
```

不要先运行不带 GLX 的安装命令，再对同一个已经变脏的 Godot 工作树重复运行安装器。安装器的正常流程是：

1. 校验 Godot Git 仓库状态和基线。
2. 对核心补丁和可选补丁执行 `git apply --check`。
3. 复制 `adapter/sources/` 到 Godot 根目录。
4. 应用通用微信适配核心补丁。
5. 复制 `adapter/thirdparty/wechat-glx/`。
6. 应用 `003-wechat-glx.patch`。

使用外部短路径 Godot checkout 时，可以把目标路径作为第一个参数：

```powershell
python adapter\scripts\apply_godot_patchset.py C:\g --include-optional wechat-glx
```

## 8. GLX 构建补丁做了什么

GLX 可选补丁只修改 Godot 的 `platform/web/detect.py`，主要行为如下。

### 8.1 新增 opt-in 构建选项

```text
wechat_glx=yes
```

默认值为 `false`。普通 Web 构建不会自动链接 GLX。

### 8.2 强制工具链匹配

启用 GLX 后，Emscripten 必须精确为 `4.0.10`。这样可以确保 `libemscriptenglx_4.0.10.a` 与当前 libc++、异常和运行时 ABI 一致。

### 8.3 链接静态库

构建环境加入：

```text
LIBPATH=#thirdparty/wechat-glx
LIBS=emscriptenglx
```

等价链接参数为：

```text
-Lthirdparty/wechat-glx
-lemscriptenglx
```

### 8.4 导出运行时方法

GLX 增加：

```text
ccall
stringToUTF8
lengthBytesUTF8
```

Godot 后续还会加入 `cwrap` 和其他自身需要的运行时方法，最终满足官方接入要求：

```text
ccall,cwrap,stringToUTF8,lengthBytesUTF8
```

### 8.5 链接和异常参数

启用 GLX 后增加：

```text
-sERROR_ON_UNDEFINED_SYMBOLS=0
-sDISABLE_EXCEPTION_THROWING=0
-sDISABLE_EXCEPTION_CATCHING=0
```

GLX 静态库会带入 Emscripten exception glue 和额外 imports。未放宽这些参数时，链接阶段可能因未定义符号或异常支持被裁掉而失败。

### 8.6 longjmp 模式

普通 Web 构建继续使用：

```text
-sSUPPORT_LONGJMP='wasm'
```

GLX 构建改用：

```text
-sSUPPORT_LONGJMP='emscripten'
```

原因是 GLX 使用的 Emscripten exception glue 与 Wasm SjLj 模式不兼容。

`WASM_BIGINT`、SIMD 默认值、raycast SIMD 条件等改动来自通用微信适配基线，不属于 `003-wechat-glx.patch` 本身，但 GLX 构建仍应使用已经验证的 `wasm_simd=no` 配置。

## 9. 编译、压缩和后处理

### 9.1 构建 GLX 模板

进入目标 Godot 根目录：

```powershell
cd godot
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes
```

关键参数：

- `platform=web`：生成 WebAssembly/Web glue。
- `target=template_release`：生成发布模板。
- `threads=no`：避免依赖 SharedArrayBuffer 和浏览器线程前提。
- `wasm_simd=no`：使用已验证的微信兼容配置。
- `wechat_glx=yes`：链接 GLX 静态库并切换 GLX 工具链参数。

普通微信 Web 构建与 GLX 构建的区别是：

```powershell
# 不链接 GLX
scons platform=web target=template_release threads=no wasm_simd=no

# 链接 GLX
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes
```

### 9.2 Brotli 压缩和 JS 后处理

Windows：

```powershell
cmd /c compress_wasm.bat
```

Linux/macOS：

```bash
./compress_wasm.sh
```

脚本实际顺序为：

1. 检查 Brotli CLI。
2. 检查 `bin/.web_zip/godot.wasm`。
3. 删除旧 `godot.wasm.br`。
4. Brotli 压缩 `godot.wasm`。
5. 执行 `node godot_process.js`。
6. 确认 `godot.wasm.br` 已生成。

因此正常流程只需要运行压缩脚本，不要再额外先运行一次 `node godot_process.js`。后处理器设计为幂等，重复运行一般不会重复注入，但没有必要制造重复步骤。

### 9.3 当前 dirty 构建现场的特殊说明

本次定位过程中，Godot 子仓库先完成了 GLX WASM 链接，随后主仓库的 `adapter/sources/godot_process.js` 才补齐最终 runtime bridge。因此保留现场中的：

```text
godot/godot_process.js
godot/bin/.web_zip/godot.js
```

仍代表“静态库已链接、bridge 尚未加入”的阶段。

正式重建应从干净基线重新运行安装器。维护者在保留 dirty 构建现场中只验证 JS 后处理时，也可以先同步源文件：

```powershell
Copy-Item ..\adapter\sources\godot_process.js .\godot_process.js -Force
node .\godot_process.js .\bin\.web_zip\godot.js
```

这只是开发现场的增量操作，不替代正式的干净安装和完整构建流程。

## 10. 为什么还需要处理生成的 godot.js

静态库进入 WASM 只提供原生实现和导出。Godot 4.5.2 的 Emscripten glue 不会自动完成微信 GLX 上下文接入。

首次构建时，WASM 和生成 JS 已经包含：

```text
_glxInit
_glxInitBufferDataAndGlState
_glxUpdateContextId
_glxCommandBufferFlush
evalRegisterAfterDoFrame
```

但生成的 `godot.js` 没有任何代码创建 `wxwebgl2` 并调用这些导出，所以真机不会出现 `libemscriptenglx` 初始化日志。

对照 Godot 4.6 模板后确认，缺失的是一段 WXGLX runtime bridge。最终 bridge 由 `adapter/sources/godot_process.js` 注入，而不是手工长期修改模板中的 `engine/godot.js`。

## 11. 后处理器的 GLX 逻辑

### 11.1 invoke import 映射

GLX 会引入多种 `invoke_*` wrapper。后处理器扫描生成 JS 中的：

```text
function invoke_...
```

并确保每个 wrapper 都进入 `wasmImports`：

```javascript
invoke_xxx: invoke_xxx
```

缺少映射时，WASM 实例化阶段会出现 import 不完整或函数不可用。

### 11.2 commit-frame shim

后处理器加入：

```javascript
var _emscripten_webgl_commit_frame = function () {};
```

并映射：

```javascript
emscripten_webgl_commit_frame: _emscripten_webgl_commit_frame
```

该函数只用于满足 WASM import。真正的 GLX 帧提交由 GLX 命令缓冲线路负责，不能把这个空 shim 当作实际渲染提交实现。

### 11.3 after-do-frame callback wrapper

生成代码中的：

```javascript
Module.wxContextGlobal.registerAfterDoFrame(Module._glxCommandBufferFlush)
```

会被改为：

```javascript
Module.wxContextGlobal.registerAfterDoFrame(function () {
  return Module._glxCommandBufferFlush();
})
```

微信的 frame hook 需要普通 JavaScript callback，直接传递 Wasm export 在部分运行时中不能正确注册。

### 11.4 runtime bridge 注入位置

bridge 注入到 Emscripten module factory 中：

```text
preInit();run();
<WXGLX runtime bridge>
addOnPostRun(function(){GL.getSource=...})
```

这个位置保证 `GL` 已定义，且引擎实际创建渲染上下文前 wrapper 已安装。

后处理器会验证：

- 四个 native export 是否完整出现。
- bridge 是否只出现一次。
- bridge 是否包含完整的 create-context 和 make-current patch。
- bridge 是否位于正确作用域和位置。
- 重复、截断或位置错误的 bridge 是否被明确拒绝。

## 12. runtime bridge 调用链

完整调用链如下：

```text
godot-loader.js
  -> 检查 __GODOT_DISABLE_WXGLX
  -> 检查 wx.env.isSupportEmscriptenGLX
  -> 固定 __godotMinigameWXGLXEnabled
  -> 创建 wxwebgl2 或 webgl2 加载画面上下文

Godot GL.createContext
  -> bridge 包装 canvas.getContext
  -> webgl2 映射为 wxwebgl2
  -> 获得 glContext
  -> 读取 glContext.emscriptenGLX
  -> glxInit(是否存在 emscriptenGLX)
  -> 首个 GLX 上下文：glxInitBufferDataAndGlState
  -> glxUpdateContextId

Godot GL.makeContextCurrent
  -> bridge 同步 glxUpdateContextId

GLX 初始化
  -> registerAfterDoFrame
  -> 每帧调用 _glxCommandBufferFlush
```

### 12.1 `glxInit`

bridge 调用：

```javascript
_glxInit(!!glContext.emscriptenGLX)
```

传入 `true` 才会出现：

```text
当前为 EmscriptenGLX 渲染方案
```

传入 `false` 会出现普通 WebGL 方案日志。

### 12.2 首次状态初始化

第一个 GLX 上下文创建时：

```javascript
Module.wxContextGlobal = Object.assign({}, glContext.emscriptenGLX)
```

然后调用：

```javascript
_glxInitBufferDataAndGlState(
  glContext.emscriptenGLX.isWebGL2 ? 2 : 1,
  glContext.emscriptenGLX.platform
)
```

`Module.wxContextGlobal` 只初始化一次，多个上下文不能重复覆盖这份全局注册信息。

### 12.3 上下文 ID

创建和切换 GLX 上下文时调用：

```javascript
_glxUpdateContextId(glContext.emscriptenGLX.ctxid)
```

否则多上下文或上下文切换后，GLX 可能向错误的 native context 提交指令。

### 12.4 官方示例与当前实现差异

微信官方示例使用 `Module.ccall(...)`。当前实现直接调用 Emscripten 暴露的：

```text
Module._glxInit
Module._glxInitBufferDataAndGlState
Module._glxUpdateContextId
```

调用参数和顺序与官方要求一致。直接调用避免每次经过 `ccall` 的字符串查找，但仍保留官方要求的运行时方法导出。

## 13. loader 上下文固定和降级开关

加载画面和 Godot 引擎使用同一个 canvas，不能让加载器先创建 `wxwebgl2`，引擎随后又尝试把同一 canvas 当作普通 `webgl2`，反之亦然。

loader 在启动时设置：

```javascript
GameGlobal.__godotMinigameWXGLXEnabled = useWXGLX
```

runtime bridge 读取这个固定模式，并校验实际上下文是否一致。如果 loader 选择 WXGLX，但引擎上下文没有 `emscriptenGLX`，bridge 会报模式不一致，而不是静默混用两个渲染线路。

为受影响设备临时关闭 GLX：

```javascript
GameGlobal.__GODOT_DISABLE_WXGLX = true
```

该开关必须在导入 `godot-loader.js` 前设置，并且只在启动时读取。建议使用独立模块：

```javascript
// glx-config.js
const gameGlobal = typeof GameGlobal !== "undefined" ? GameGlobal : globalThis;

if (gameGlobal.__GODOT_DISABLE_WXGLX === undefined) {
  gameGlobal.__GODOT_DISABLE_WXGLX = false;
}
```

入口加载顺序：

```javascript
import "./weapp-adapter";
import "./glx-config";
import "./godot-loader";
```

不要只在同一个 ES module 的静态 import 语句后写赋值并假设它会先于依赖执行。ES module 依赖会先求值，独立的 `glx-config.js` 更明确。

`__godotMinigameWXGLXEnabled` 是 loader 和 engine 之间的内部模式标记，不应把它当作用户配置开关。用户配置只使用 `__GODOT_DISABLE_WXGLX`。

## 14. 什么时候需要重新编译

| 修改内容 | 是否需要重新运行 SCons | 后续操作 |
| --- | --- | --- |
| `platform/web/detect.py`、链接参数 | 需要 | 重新构建、压缩、后处理、打包 |
| `libemscriptenglx.a` 版本或变体 | 需要 | 完整重新构建和真机回归 |
| Godot C/C++ 或 JS library | 需要 | 完整重新构建 |
| WASM 缺少四个 GLX export | 需要 | 修复链接后重新构建 |
| 只修改 `godot_process.js` bridge | 不需要重新编译 WASM | 重新处理 `godot.js` 并打包 |
| 只修改 `godot-loader.js` | 不需要 | 更新模板并打包 |
| 只修改模板文档或图片 | 不需要 | 更新模板并打包 |

本次最终修复只补齐了生成 JS 的 runtime bridge。已有 WASM 已正确链接 GLX，所以 `godot.wasm.br` 保持不变，只重新处理了 `godot.js` 并重建 `.tpz`。

## 15. 自动化测试和静态验证

### 15.1 bridge 测试

从主仓库根目录运行：

```powershell
node adapter\tests\test_godot_process_glx.js
```

测试覆盖：

- 首次 bridge 注入
- 重复处理幂等性
- 重复、截断和位置错误 bridge 拒绝
- native exports 不完整时失败
- `webgl2 -> wxwebgl2`
- `glxInit` 调用
- 首次 BufferData/GL 状态初始化
- 多上下文切换和 context ID 更新
- 普通 WebGL 回退
- 显式禁用 GLX
- 缺少 native bindings
- pinned `wxwebgl2` 创建失败
- loader/engine 上下文模式不一致

### 15.2 loader 测试

```powershell
node adapter\tests\test_min_runtime_loader.js adapter\assets\min-runtime\godot-loader.js
node adapter\tests\test_min_runtime_loader.js templates\4.5.2\minigame4.5.2_glx\godot-loader.js
```

预期覆盖 `wxwebgl2`、显式禁用和不支持时的 `webgl2` 选择。

### 15.3 JavaScript 语法

```powershell
node --check adapter\sources\godot_process.js
node --check templates\4.5.2\minigame4.5.2_glx\godot-loader.js
node --check templates\4.5.2\minigame4.5.2_glx\engine\godot.js
```

### 15.4 生成 JS 关键标记

```powershell
rg -n "wxGLXGetNativeExport|wxGLXPatchCreateContext|wxwebgl2|glxInitBufferDataAndGlState|glxUpdateContextId|glxCommandBufferFlush" `
  godot\bin\.web_zip\godot.js
```

必须确认：

- 四个 GLX native export 都存在。
- runtime bridge 只出现一次。
- `wxwebgl2` 映射存在。
- after-do-frame callback 已包装。

### 15.5 验证 WASM exports/imports

下面的 Node 脚本直接解压 Brotli，并检查 WebAssembly 模块：

```javascript
const fs = require("fs");
const zlib = require("zlib");

const compressed = fs.readFileSync(
  "templates/4.5.2/minigame4.5.2_glx/engine/godot.wasm.br"
);
const wasm = zlib.brotliDecompressSync(compressed);
const module = new WebAssembly.Module(wasm);
const exports = new Set(
  WebAssembly.Module.exports(module).map((item) => item.name)
);
const imports = new Set(
  WebAssembly.Module.imports(module).map((item) => item.name)
);

for (const name of [
  "glxInit",
  "glxInitBufferDataAndGlState",
  "glxUpdateContextId",
  "glxCommandBufferFlush",
]) {
  if (!exports.has(name)) {
    throw new Error(`missing WASM export: ${name}`);
  }
}

if (!imports.has("evalRegisterAfterDoFrame")) {
  throw new Error("missing WASM import: evalRegisterAfterDoFrame");
}

console.log("WXGLX WASM exports/imports are complete");
```

### 15.6 Brotli 完整性

```powershell
brotli --test templates\4.5.2\minigame4.5.2_glx\engine\godot.wasm.br
```

## 16. 模板组装和打包

模板目录结构：

```text
minigame4.5.2_glx/
├── game.js
├── game.json
├── godot-loader.js
├── weapp-adapter.js
├── BUILD_INFO.md
├── engine/
│   ├── game.js
│   ├── godot-sdk.js
│   ├── godot.js
│   ├── godot.wasm.br
│   └── demo-pck.bin
├── images/
└── subpack1/
```

生成物映射：

```text
godot/bin/.web_zip/godot.js
  -> templates/4.5.2/minigame4.5.2_glx/engine/godot.js

godot/bin/.web_zip/godot.wasm.br
  -> templates/4.5.2/minigame4.5.2_glx/engine/godot.wasm.br
```

复制前必须确认目标 `godot.js` 已包含最终 bridge。不要因为 WASM 哈希相同，就把旧的无 bridge `godot.js` 一起复制过去。

更新 `BUILD_INFO.md` 中：

- Godot 基线
- Emscripten 版本
- GLX 版本
- 构建命令
- Brotli 版本
- `godot.js` SHA-256
- `godot.wasm.br` SHA-256

### 16.1 不要增量更新旧归档

打包时应删除旧 `.tpz`，再从最终模板目录完整创建新归档。不能使用可能保留旧条目的增量 ZIP 更新方式。

PowerShell 示例：

```powershell
Add-Type -AssemblyName System.IO.Compression.FileSystem

$source = (Resolve-Path "templates/4.5.2/minigame4.5.2_glx").Path
$archive = [System.IO.Path]::GetFullPath(
  "templates/4.5.2/minigame4.5.2_glx.tpz"
)

if (Test-Path $archive) {
  Remove-Item -Force $archive
}

[System.IO.Compression.ZipFile]::CreateFromDirectory(
  $source,
  $archive,
  [System.IO.Compression.CompressionLevel]::Optimal,
  $false
)
```

这种方式会包括 `.eslintrc.js` 等隐藏文件，并让模板内容成为归档的根目录，而不是再套一层 `minigame4.5.2_glx/`。

### 16.2 归档检查

```powershell
unzip -t templates\4.5.2\minigame4.5.2_glx.tpz
unzip -Z1 templates\4.5.2\minigame4.5.2_glx.tpz
```

必须检查：

- ZIP 完整性通过。
- 没有重复条目。
- 每个源文件都进入归档。
- 归档中的 `godot.js`、loader 和 WASM 与模板目录逐字节一致。
- 不包含 `.tmp`、`.bak`、旧 ZIP、日志或调试文件。
- 不包含此前临时加入的 `[Godot WXGLX]` 自定义诊断。

### 16.3 微信开发者工具配置污染

微信开发者工具会自动改写：

```text
project.config.json
project.private.config.json
```

可能变化的内容包括：

- AppID
- `libVersion`
- 本地调试设置
- Source Map 设置
- LAN 调试和热重载设置

制作公共模板前必须明确哪些配置应保留，避免把个人 AppID 或只适用于本机的私有配置误打进 Release。不要把开发者工具自动写入的所有字段都当作 GLX 必需配置。

### 16.4 发布索引

如果把 4.5.2 GLX 模板作为插件可下载模板发布，还需要在 `resources/versions.yaml` 中登记准确版本、Release tag 和 `.tpz` 文件名。登记前应先确定 `minigame4.5.2_glx.tpz` 是否作为 4.5.2 默认模板，避免同一 Godot 版本出现不明确的多个默认文件。

## 17. 开发者工具和真机验收

### 17.1 开发者工具

本次实测开发者工具会使用普通 WebGL：

```text
platform: devtools
wx.env.isSupportEmscriptenGLX: false
当前为普通 WebGL/WebGL2 渲染方案
```

Godot 可能报告：

```text
llvmpipe: WebKit WebGL
```

这只能证明普通 WebGL fallback 和 bridge 的 `glxInit(false)` 路径可运行，不能据此判定 Android 真机 GLX 失败。

### 17.2 真机能力检查

可以辅助查看：

```javascript
wx.getSystemInfoSync().platform
wx.getSystemInfoSync().SDKVersion
wx.env.isSupportEmscriptenGLX
GameGlobal.__godotMinigameWXGLXEnabled
```

含义：

- `isSupportEmscriptenGLX === true`：设备和基础库声明支持。
- `__godotMinigameWXGLXEnabled === true`：loader 固定选择了 WXGLX 模式。
- 四条官方初始化日志：实际上下文和原生初始化成功。

前两个值是辅助信号，不能替代官方日志和正常渲染。

### 17.3 真机调试日志噪声

真机调试初期可能出现：

```text
jsbridge not ready
```

这是微信调试桥启动阶段日志。除非后续引擎加载中断或同时出现 GLX 初始化错误，否则不能直接把它当作 GLX 失败原因。

### 17.4 上传体验版验收

最终验收应使用上传后的体验版或实际发布版本：

1. 上传包含最新模板的小游戏版本。
2. 退出旧小游戏。
3. 清理缓存或确认版本号已更新。
4. 在 Android 真机或满足条件的 iOS 真机重新进入。
5. 打开 vConsole 或远程日志。
6. 查找四条官方成功日志。
7. 检查场景是否正常渲染、切换、暂停和恢复。
8. 检查帧率和单帧耗时是否相对普通 WebGL 出现异常。

本次 Android 上传体验版最终出现全部四条成功日志，确认线路已经实际启用。

## 18. 常见问题排查

| 现象 | 代表什么 | 优先检查 | 处理 |
| --- | --- | --- | --- |
| 没有 `Wechat GLX Lib` 日志 | `_glxInit` 未被调用 | bridge 是否存在，WASM exports 是否完整 | 重新安装最新后处理器并处理 `godot.js` |
| 有版本日志，但显示普通 WebGL | bridge 已运行，但上下文无 `emscriptenGLX` | 当前平台、support、disable flag、上下文类型 | 开发者工具属预期；真机继续检查 loader 模式 |
| `isSupportEmscriptenGLX=true`，但无官方日志 | 只有设备能力，没有执行初始化链 | `wxGLXPatchCreateContext` 和 `_glxInit` | 补齐生成 JS bridge |
| 缺少 native binding 错误 | WASM 导出不完整或使用了非 GLX WASM | 四个 `_glx*` export | 使用 `wechat_glx=yes` 重新编译 |
| Canvas context mode mismatch | loader 和 engine 使用了不同上下文模式 | 导入顺序、全局对象、禁用开关 | 保证配置模块先于 loader，禁止运行中切换 |
| Failed to create pinned wxwebgl2 | 设备声明支持但上下文创建失败 | 基础库、客户端、iOS 能力、运行环境 | 临时禁用 GLX并记录设备信息 |
| 黑屏或渲染异常 | GLX 设备兼容、帧提交或项目渲染问题 | 普通 WebGL 对照、官方四条日志 | 用禁用开关回退并分别验证 Android/iOS |
| `registerAfterDoFrame` 不出现 | BufferData 初始化或 callback 注册未完成 | callback wrapper、`wxContextGlobal` | 检查后处理规则和 native 调用异常 |
| 链接时报 Emscripten 版本错误 | 静态库变体与工具链不匹配 | `emcc --version` | 激活 Emscripten 4.0.10 |
| WASM 实例化提示 invoke/import 缺失 | GLX 新增 imports 未映射 | `invoke_*`、`wasmImports` | 运行最新 `godot_process.js` |
| 修改后仍看到旧日志 | 小游戏或 `.tpz` 仍是旧版本 | 归档哈希、上传版本、缓存 | 删除旧归档重建并清理手机缓存 |
| `git status` 看不到 WASM 或 `.a` | 文件被 Godot `.gitignore` 忽略 | `bin/` 和 `*.a` 规则 | 直接检查路径、大小和 SHA-256 |

## 19. 本次定位复盘

### 19.1 第一阶段：引入官方静态库

主仓库提交 `d22ead3` 首先引入：

```text
adapter/thirdparty/wechat-glx/libemscriptenglx.a
adapter/thirdparty/wechat-glx/SOURCE.md
adapter/thirdparty/wechat-glx/version.txt
```

这个阶段只完成依赖入库，还不构成完整 GLX 适配。

### 19.2 第二阶段：加入构建选项并链接

新增 `003-wechat-glx.patch`，解决：

- 静态库路径和链接名称
- Emscripten 版本约束
- 运行时方法导出
- exception glue
- undefined symbols
- `SUPPORT_LONGJMP` 模式

随后使用：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no wechat_glx=yes
```

成功生成包含 GLX 的 WASM。

### 19.3 第三阶段：静态检查看似完整，真机却没有日志

静态检查确认 WASM 已包含：

```text
glxInit
glxInitBufferDataAndGlState
glxUpdateContextId
glxCommandBufferFlush
```

也包含 GLX 版本和官方日志字符串，但 Android 真机最初没有任何 `libemscriptenglx` 初始化日志。

这个现象说明问题不是“静态库没有进入 WASM”，而是“没有 JavaScript 代码调用原生入口”。

### 19.4 第四阶段：区分支持信号和实际启用

临时 loader 诊断用于观察：

- platform
- `isSupportEmscriptenGLX`
- disable flag
- 请求的 context type
- context 是否创建

这一步确认 Android 和基础库满足支持条件，也确认开发者工具会回退普通 WebGL。但自定义诊断只能帮助定位，不能替代 GLX 官方日志。

临时 `[Godot WXGLX]` 日志和 `__godotMinigameWXGLXDiagnostics` 最终已经从模板中删除，不应重新作为正式接口发布。

### 19.5 第五阶段：对照 4.6 找到缺失 bridge

对比 4.6/4.7 生成的 `godot.js` 后发现，它们包含完整 WXGLX JS bridge，而 4.5.2 生成物完全缺失。

4.5.2 当时的生成物状态为：

```text
godot.js: 375875 bytes
SHA-256: 5F411D2363C14E5E4E05246D21E925B064406AFD7C8BDC4339F0B5545EE8E752
bridge copies: 0
```

它有四个 native export，却没有创建 WXGLX 上下文和调用初始化函数的执行线路。

### 19.6 第六阶段：移植 bridge，不重新编译 WASM

bridge 被加入 `adapter/sources/godot_process.js`，再用于处理现有 `engine/godot.js`。

最终 JS 状态：

```text
godot.js: 382756 bytes
SHA-256: A1C616384A00D405947BF55A7CE56A96FCBB399ADBADA4E0E4383A7D85759B5B
bridge copies: 1
```

WASM Brotli 文件保持：

```text
SHA-256: 7BC3F63F959FEC0F5F94522D9D995F51D00172B3DC9202D5DEEAB3EDD3B88EAE
```

这证明最终功能差异只来自 JS bridge，不需要重新编译已经正确链接的 WASM。

### 19.7 第七阶段：开发者工具和真机结果分离

开发者工具随后出现：

```text
Wechat GLX Lib: 0.1.11
当前为普通 WebGL/WebGL2 渲染方案
```

这证明 bridge 已经开始调用 `_glxInit(false)`，但开发者工具仍是普通 WebGL 环境。

上传体验版后，Android 真机出现：

```text
Wechat GLX Lib: 0.1.11
当前为 EmscriptenGLX 渲染方案
registerAfterDoFrame register
完成BufferData逻辑初始化
```

最终确认完整线路正确。

## 20. 当前验证产物

当前已验证模板：

```text
templates/4.5.2/minigame4.5.2_glx.tpz
```

SHA-256：

```text
87C8AF8D82B129E7D24B992DCC14BF9E23A5ED71587CA03D9B0AD440EAE7129F
```

大小：

```text
8,487,998 bytes
```

归档验证结果：

- 20 个文件条目
- 无重复条目
- ZIP 完整性通过
- 20 个归档文件与模板目录逐文件 SHA-256 一致
- `godot.js` 和 loader 语法检查通过
- Brotli WASM 完整性通过
- 临时自定义 GLX 诊断计数为 0
- runtime bridge 计数为 1
- Android 真机官方四条日志通过

## 21. 维护和发布检查清单

- [ ] Godot HEAD 精确为 `6ce3de25aa58466e14ef354703ba8d9791a417da`
- [ ] Godot 工作树在首次应用补丁前干净
- [ ] 使用一次安装命令同时加入 `wechat-glx`
- [ ] `libemscriptenglx.a` SHA-256 正确
- [ ] Emscripten 精确为 `4.0.10`
- [ ] 构建命令包含 `wechat_glx=yes`
- [ ] 构建命令包含 `threads=no wasm_simd=no`
- [ ] `compress_wasm.bat` 或 `compress_wasm.sh` 成功
- [ ] `test_godot_process_glx.js` 通过
- [ ] loader GLX 选择测试通过
- [ ] 生成 `godot.js` 中 bridge 只出现一次
- [ ] 四个 native export 和 `evalRegisterAfterDoFrame` 完整
- [ ] `godot.wasm.br` Brotli 校验通过
- [ ] `BUILD_INFO.md` 哈希已更新
- [ ] 旧 `.tpz` 已删除并完整重建
- [ ] 归档无重复和临时文件
- [ ] 公共模板没有意外泄露个人 AppID 或本机私有设置
- [ ] 开发者工具普通 WebGL fallback 正常
- [ ] Android 上传体验版出现四条官方成功日志
- [ ] iOS 发布前在实际高性能+设备单独验证
- [ ] 黑屏或兼容问题可以通过启动前禁用开关回退
- [ ] Release 发布时模板文件名与 `resources/versions.yaml` 一致

## 22. 官方资料

- [EmscriptenGLX 原生引擎接入](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-emscriptenglx-native.html)
- [EmscriptenGLX 方案介绍及 Q&A](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-emscriptenglx.html)
- [EmscriptenGLX 更新日志](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-emscriptenglx-changelog.html)
- [iOS 高性能+模式](https://developers.weixin.qq.com/minigame/dev/guide/performance/perf-high-performance-plus.html)
- [仓库静态库来源记录](thirdparty/wechat-glx/SOURCE.md)
- [GLX 可选补丁](patches/optional/003-wechat-glx.patch)
- [生成 JS 后处理器](sources/godot_process.js)
- [GLX bridge 测试](tests/test_godot_process_glx.js)
- [模板构建信息](../templates/4.5.2/minigame4.5.2_glx/BUILD_INFO.md)
