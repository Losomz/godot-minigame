# Godot 4.5 微信小游戏适配补丁包

此包用于从官方 Godot 源码构建可继续开发的微信小游戏适配版本。补丁锁定以下两个提交：

- 官方基线：`d1996aadb3672d877a7ae5df772b45127cf2c77a`
- 适配来源：`e95971c17e4890b24d52824eb1d045f7730f8771`

基线是官方 `4.5` 分支的 `4.5.2-rc` 快照，不是 `4.5.1-stable`，也不是当前 `4.5` 分支最新提交。现有适配分支已经合入该快照之前的官方更新；固定基线才能保证补丁结果可重复。

## 应用补丁

```powershell
git clone https://github.com/godotengine/godot.git godot-4.5-wechat
cd godot-4.5-wechat
git checkout d1996aadb3672d877a7ae5df772b45127cf2c77a
python <补丁包>\skills\scripts\apply_godot_patchset.py . --bundle godot-4.5.2-rc-d1996aadb3
```

脚本默认拒绝脏工作区和错误基线。不要使用 `--allow-base-mismatch` 作为常规安装方式。

如果目标是工具箱导出流程，首次应用时改用下面的命令，同时加入编辑器导出 API。不要先执行上面的核心命令再重复应用：

```powershell
python <补丁包>\skills\scripts\apply_godot_patchset.py . --bundle godot-4.5.2-rc-d1996aadb3 --include-optional export-api
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

Windows 下应将 Godot 源码放在短路径（例如 `C:\g`），避免最终 Emscripten 链接命令超过 `cmd.exe` 长度限制。

保留 2D、中文、音频、网络和常见资源格式的通用小游戏裁切构建：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no profile=configs/wechat_2d.py
```

`configs/wechat_2d.py` 按当前约 4.9 MB 产物记录每个关键模块的实际
开关状态和中文说明，可直接修改后构建新的裁切变体。

## 小游戏运行壳

源码适配仍需要宿主提供 `fsUtils`、`GODOTSDK`、`nowPolyfill` 和加载器。安装包内最小运行壳：

```powershell
python <补丁包>\skills\scripts\install_min_runtime.py <小游戏工程目录>
```

加载顺序必须是 `godot-sdk.js`、`godot-loader.js`、生成的 `godot.js`。

## 范围

核心包含构建接线、WXMEMFS、`wx.request`、长音频原生播放、显示/输入适配和 JS 后处理。未引用的 `audio.worker.js`、废弃的 `library_godot_wx_audio.js`、本地配置、历史补丁快照和实验文件未纳入。
