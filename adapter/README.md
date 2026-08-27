# Godot 4.5 微信小游戏适配补丁包

此包用于从官方 Godot 源码构建可继续开发的微信小游戏适配版本。补丁锁定以下两个提交：

- 官方基线：`6ce3de25aa58466e14ef354703ba8d9791a417da`
- 适配来源：`e95971c17e4890b24d52824eb1d045f7730f8771`

基线是官方发布的 `4.5.2-stable`，不是当前 `4.5` 分支最新提交。固定基线才能保证补丁结果可重复。

## 应用补丁

```powershell
git submodule update --init adapter/thirdparty/godot
python adapter\scripts\apply_godot_patchset.py
```

脚本默认拒绝脏工作区和错误基线。不要使用 `--allow-base-mismatch` 作为常规安装方式。

如果目标是工具箱导出流程，首次应用时改用下面的命令，同时加入编辑器导出 API。不要先执行上面的核心命令再重复应用：

```powershell
python adapter\scripts\apply_godot_patchset.py --include-optional export-api
```

其他可选项：`branding` 只修改版本标识；`dev-types` 只安装微信 API 类型声明。

## 构建

已验证 Emscripten `4.0.10`：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no
cmd /c compress_wasm.bat
node platform/web/js/tests/test_wechat_audio_context_single_release.js
```

`compress_wasm.bat` 同时生成 `bin/.web_zip/godot.wasm.br` 并修补 `godot.js`，不能省略。

GLX 模板需要应用 `wechat-glx` optional feature；详细的开发构建参数见 [`WECHAT_GLX.md`](WECHAT_GLX.md)。仓库维护者从 manifest 锁定且已提交的 Godot 子模块生成正式模板时运行：

```powershell
python adapter\scripts\package_wechat_glx_template.py
```

该命令默认执行干净 SCons 构建、Brotli/JS 后处理、ABI 验证和 `.tpz` 回读校验。

Windows 下应将 Godot 源码放在短路径（例如 `C:\g`），避免最终 Emscripten 链接命令超过 `cmd.exe` 长度限制。

保留 2D、中文、音频、网络和常见资源格式的通用小游戏裁切构建：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no profile=adapter/configs/wechat_2d.py
```

`adapter/configs/wechat_2d.py` 按实测产物记录每个关键模块的实际
开关状态和中文说明，可直接修改后构建新的裁切变体。

### GLX 体积与 C++ 异常（2026-08 实测）

同一 `adapter/configs/wechat_2d.py` 裁切配置下：非 GLX ≈ 4.85 MiB；
GLX + 异常开启 ≈ 6.05 MiB（默认产物）；GLX + 异常关闭 ≈ 4.91 MiB。GLX
静态库本身仅约 +67 KB，约 1.14 MiB 的差异几乎全部来自 C++ 异常支持。
GLX 构建默认开启异常（`wechat_glx_exceptions=yes`，微信
`libemscriptenglx.a` 内部会 `throw`）；测试用 `enabled`，游戏完成发布可
`disabled` 省约 1.14 MiB，代价是 GLX 库抛异常时直接 abort。

统一入口（模板基底 + 裁切模板 + 变体组合）：

```powershell
python adapter\ci\package.py --list
python adapter\ci\package.py --template 4.5.2 --variant glx --profile 2d --exceptions disabled --revision 2
```

裁切清单 = `adapter/configs/*.py`（每个文件一个变体，`--profile` 简名
选择，也接受任意路径）。完整数据与说明见
[`WECHAT_GLX.md`](WECHAT_GLX.md)「包体与 C++ 异常」章节。

## 小游戏运行壳

源码适配仍需要宿主提供 `fsUtils`、`GODOTSDK`、`nowPolyfill` 和加载器。安装包内最小运行壳：

```powershell
python adapter\scripts\install_min_runtime.py <小游戏工程目录>
```

加载顺序必须是 `godot-sdk.js`、`godot-loader.js`、生成的 `godot.js`。

## 范围

核心包含构建接线、WXMEMFS、`wx.request`、长音频原生播放、显示/输入适配和 JS 后处理。未引用的 `audio.worker.js`、废弃的 `library_godot_wx_audio.js`、本地配置、历史补丁快照和实验文件未纳入。
