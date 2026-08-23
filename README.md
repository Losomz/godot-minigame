# Godot Minigame

面向 Godot 4.4+ 的 C++ 编辑器插件，提供小游戏模板适配、下载、缓存、解压和导出能力。

插件本体更新不通过模板分发流程管理。模板通过 Release 资产分发，插件本身建议通过 Godot Asset Library 或仓库源码安装更新。

如果要用 AI 协助做适配移植，请使用 `.agent/skills/godot-wechat-minigame-adapter/`。Godot 4.5.2 GLX 的原理、源码改动和产物流程见 [`adapter/WECHAT_GLX.md`](adapter/WECHAT_GLX.md)。

## 功能

- 按当前 Godot 版本自动匹配模板
- 支持 GitHub / Gitee Release 作为模板分发源
- 首次下载模板，后续走本地缓存
- 导出时自动解压模板并生成小游戏目录
- 支持将资源嵌入扩展产物
- 提供可选的微信小游戏广告接入组件
- 提供独立的 Godot 4.5 微信适配源码与 Agent Skill

## 构建

```bash
git clone <repo-url>
git submodule update --init godot-cpp
./build_osx.sh
```

也可以按平台选择：

- `build_win.bat`
- `./build_linux.sh`
- `./build_osx.sh`

这些脚本默认都会以 `embed_resources=yes` 构建 release 产物。

构建产物默认会安装到：

`demo/addons/godot-minigame/bin/<platform>/`

## 使用

1. 将 `demo/addons/godot-minigame/` 放到你的 Godot 项目 `res://addons/godot-minigame/`
2. 在编辑器里启用插件
3. 在设置面板配置模板分发源：
   `Source / Owner / Repo / Tag`
4. 导出时插件会自动选择模板并完成下载、缓存和解压

## 模板分发约定

Release 需要提供：

- 索引 Release：全量 `versions.yaml`
- 模板 Release：对应版本的 `*.tpz`

示例：

```yaml
godot4:
  4.5.1:
    tag: 4.5.1
    file: minigame4.5.1.tpz
```

当前匹配规则：

- 先找当前引擎版本的精确匹配
- 找不到则选择同大版本下不高于目标版本的最新模板
- 还找不到则回退到该大版本桶里的最后一个条目

## 微信广告

独立的微信小游戏广告组件位于 [`adapter/wechat_ad/`](adapter/wechat_ad/README.md)，支持激励视频、插屏广告和原生模板广告。组件不包含项目广告位、AppID 或奖励发放逻辑。打包时通过 `ci/package.py --ad` 将广告桥融合进模板（产物名带 `-ad`）。

## 目录

- `src/`：源码
- `include/`：头文件
- `resources/`：嵌入资源与发布索引（`versions.yaml`）
- `templates/`：打包基底库（模板格式文件 + 裁切模板 `configs/` + 登记表 `manifest.json`）
- `adapter/`：Godot 4.5 微信适配层（引擎补丁、运行壳、模板模具、裁切配置、微信广告组件、打包构建器）
- `dist/`：打包产物出口（gitignored，完整模板即插即用；正式分发走 GitHub Release）
- `skills/`：Godot 4.6.2 自包含适配技能包（独立分发单元，与 `adapter/` 版本对应）
- `.agent/skills/`：本仓库 Agent 技能入口，只引用 `adapter/` 与 `skills/`，不复制内容
- `demo/`：插件示例项目
- `ci/`：统一打包入口（`package.py`）与构建引导（SCons 环境、依赖清单）
- `tools/`：构建辅助脚本
- `godot-cpp/`：submodule
- `godot/`：可选的官方 Godot 4.5 submodule，由 gitlink 锁定精确基线

打包入口见 [`templates/README.md`](templates/README.md) 与 `python ci/package.py --list`；产物选择见 `dist/README.md`。

维护引擎适配时再初始化官方 Godot 源码；普通克隆不会下载该子模块：

```bash
git submodule update --init godot
python adapter/scripts/apply_godot_patchset.py
```

## License

MIT，见 `LICENSE`。
