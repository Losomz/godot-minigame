# Godot Minigame

面向 Godot 4.4+ 的微信小游戏工具链。仓库发布两类产品：Godot 编辑器插件和经过验证的微信小游戏模板。

## 仓库结构

```text
.agent/       适配层 Skill 入口
adapter/      Godot 4.5 微信小游戏适配基线
plugin/       编辑器插件源码、资源、构建和发布输入
demo/         最小 Godot 插件调试项目
tests/        插件、适配层和仓库契约测试
tools/        跨模块开发与发布工具
docs/         架构、贡献和发布文档
dist/         本地生成制品，不进入 Git
```

根目录不保存插件安装形态。标准 `addons/godot-minigame/` 只在 demo 组装目录、插件打包暂存目录和 Release ZIP 中生成。

详细职责见 [仓库架构](docs/architecture.md)，分支和发布规则见 [分支与发布](docs/branching-and-releases.md)。

## 插件

插件全部维护在 `plugin/`：

- `plugin/addon/`：GDScript 和 GDExtension 描述文件
- `plugin/src/`、`plugin/include/`：原生插件实现
- `plugin/resources/`：嵌入资源
- `plugin/build/`：SCons 配置和平台构建脚本
- `plugin/thirdparty/godot-cpp/`：编译依赖
- `plugin/catalog/`：插件和模板发布目录
- `plugin/plugin.json`：插件产品清单

构建原生插件：

```bash
git submodule update --init plugin/thirdparty/godot-cpp
scons -f plugin/build/SConstruct platform=windows arch=x86_64 target=template_release embed_resources=yes
```

原生库生成到 `dist/plugin/native/<platform>/`。验证并打包：

```bash
python tools/product/product.py validate
python -m unittest discover -s tests -v
python tools/product/product.py package-plugin
```

插件 ZIP 生成到 `dist/plugin/`，内部安装路径固定为 `addons/godot-minigame/`。

## 日常调试

`demo/` 是进入 Git 的最小测试项目，但 `demo/addons/godot-minigame/` 是本地生成目录，不保存第二份插件源码。

```powershell
pwsh tools/dev/build_demo.ps1
```

脚本从 `plugin/addon/` 和 `dist/plugin/native/` 组装 demo 插件。随后打开 `demo/project.godot`。

## 适配层

`develop/main` 以 Godot 4.5 适配为公共基线，入口位于 `adapter/`，配套 Skill 位于 `.agent/skills/godot-wechat-minigame-adapter/`。

```powershell
git submodule update --init adapter/thirdparty/godot
python adapter/scripts/apply_godot_patchset.py --include-optional export-api
python adapter/ci/package.py --list
```

补丁、模板基底、运行时源码、构建配置和适配工具都归属于 `adapter/`。后续版本适配分支从该基线升级，不向产品分支反向合并版本差异。

## 文档

- [仓库架构](docs/architecture.md)
- [分支与发布](docs/branching-and-releases.md)
- [贡献指南](docs/CONTRIBUTING.md)
- [更新记录](docs/CHANGELOG.md)
- [行为准则](docs/CODE_OF_CONDUCT.md)
- [安全策略](docs/SECURITY.md)

## License

MIT，见 `LICENSE`。
