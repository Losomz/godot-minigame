# Template Obfuscation

对微信小游戏模板的自有启动脚本执行确定性的轻度混淆。源模板不会被修改。

当前处理：

- `game.js`
- `godot-loader.js`
- `engine/game.js`

当前不处理 `engine/godot.js`、`engine/godot-sdk.js`、WASM 和 PCK，避免破坏
Emscripten、WXMEMFS 和微信运行时接口。

安装依赖：

```powershell
npm install --prefix tools/obfuscation
```

生成测试模板：

```powershell
node tools/obfuscation/obfuscate_template.js `
  templates/4.5.1_2d/minigame4.5.1_2d `
  dist/4.5.1_2d_obfuscated `
  --seed 451201
```

不同项目应使用不同但固定的 seed。不要使用随机 seed，否则相同源码无法复现
相同发布产物。
