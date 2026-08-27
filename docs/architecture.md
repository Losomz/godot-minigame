# 仓库架构

## 所有权边界

`adapter/`、`plugin/`、`demo/` 和 `tests/` 是仓库的四个主要工作区。文件应按所有权进入对应目录，不允许为方便打包重新在根目录建立源码副本。

### adapter

维护 Godot 微信小游戏适配基线，包括 Skill 指向的补丁、源码覆盖、运行时壳、构建配置、模板基底、适配脚本和 Godot gitlink。Godot checkout 固定在 `adapter/thirdparty/godot/`。

### plugin

维护编辑器插件的全部构建输入。`plugin/addon/` 是可安装 addon 的源码输入，但不是最终安装目录；打包和 demo 脚本负责生成标准 `addons/godot-minigame/`。

### demo

只保存最小 Godot 项目。`demo/addons/godot-minigame/` 和 `.godot/` 均为本地生成内容，不进入 Git。

### tests

- `tests/plugin/`：插件行为和打包测试
- `tests/adapter/`：适配层 JavaScript 回归测试
- `tests/contracts/`：顶层结构和所有权约束

## 制品

所有本地制品进入被忽略的 `dist/`：

```text
dist/plugin/native/    原生插件库
dist/plugin/staging/   插件 ZIP 暂存目录
dist/plugin/*.zip      插件发布候选
dist/template/         模板发布候选
dist/build-tools/      临时构建工具
```

标准 Godot addon 安装结构只允许出现在 `demo/addons/`、`dist/plugin/staging/addons/` 和 Release ZIP 内。
