# Godot 4.5.1 2D WeChat Template

这是 Godot 4.5 微信小游戏的候选 2D 模板变体，目前不在
`resources/versions.yaml` 中发布，不会替换现有正式模板。

## 引擎基线

- Godot base: `d1996aadb3672d877a7ae5df772b45127cf2c77a`
- Patch bundle: `godot-4.5.2-rc-d1996aadb3`
- Emscripten: `4.0.10`
- Template compatibility bucket: `4.5.1`

## 裁切配置

配置文件：

`godot-4.5-wechat-minigame-patchset-e95971c/configs/wechat_2d.py`

构建命令：

```powershell
scons platform=web target=template_release threads=no wasm_simd=no profile=<patch-kit>/configs/wechat_2d.py
```

当前主要裁切完整 3D 和 XR，保留 2D 物理、2D 导航、中文、音频、
HTTP/WebSocket、TLS、常见图片格式和 JavaScriptBridge。

## 当前产物

- `engine/godot.wasm.br`: 5,081,102 bytes，约 4.85 MiB
- `engine/godot.js`: 333,649 bytes
- `engine/demo-pck.bin`: 3,128 bytes

正式发布前仍需完成微信开发者工具和真机功能回归。
