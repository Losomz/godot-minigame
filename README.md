# Godot Minigame

面向 Godot 4.4+ 的微信小游戏产品线。仓库只发布两种产品：Godot 编辑器插件和经过验证的小游戏模板。

插件与模板是独立发布物：插件使用 `plugin-v*` Release，模板使用 `<godot>-<variant>-r*` Release（如 `4.5.2-glx-2d-r1`）。插件更新不依赖仓库全局 `latest`，模板也不会触发插件升级。

## 分支结构

- `upstream-sync`：`citizenll/godot-minigame:main` 的只读同步线。
- `main`：稳定产品主线，保存插件、模板 Catalog、中央 workflow 和发布规则。
- `develop`：日常产品开发分支；临时功能和修复分支直接从这里创建并合回这里。
- `4.5`、`4.6` 等版本分支：模板适配生产线，不直接合入 `main` 或 `develop`。

`refactor/*`、`feature/*` 和 `fix/*` 都是一次性工作分支，不构成额外层级。适配分支完成构建后将不可变模板上传到 Release；Promote workflow 验证来源 commit 和 SHA-256，然后只向 `main` 提交模板 Catalog 与生成索引。

详细规则见 [`docs/branching-and-releases.md`](docs/branching-and-releases.md)。

## 功能

- 按当前 Godot 版本匹配已晋升模板
- 支持 GitHub、Gitee 和 AtomGit Release 模板源
- 模板下载、缓存、解压和导出
- 插件与模板独立版本和更新通道
- 可选微信小游戏广告组件
- 可复现的 Godot 微信适配补丁与验证技能

## 构建插件

```bash
git submodule update --init --recursive
scons platform=windows arch=x86_64 target=template_release embed_resources=yes
```

也可以使用：

- `build_win.bat`
- `./build_linux.sh`
- `./build_osx.sh`

原生库统一生成到 `dist/plugin/native/<platform>/`。

验证产品契约并组装可安装插件：

```bash
python tools/product/product.py validate
python -m unittest discover -s tests/product -v
python tools/product/product.py package-plugin
```

插件包生成在 `dist/plugin/`，组装目录为 `dist/plugin/staging/`，ZIP 内部固定为 `addons/godot-minigame/`。`dist/` 是统一临时出口，整体不进入 Git，删除后可由构建和打包命令重新生成。

如需生成离线插件包，可选择把已登记在模板 Catalog 中的本地 TPZ 一起打包：

```bash
python tools/product/product.py package-plugin \
  --bundle-template /path/to/minigame4.5.1.2.tpz
```

TPZ 在插件 ZIP 中只保存一份。导出预设的“模板/模板来源”可选择“自动”或“仅插件内模板”；自动模式优先使用兼容的插件内模板，否则使用本地缓存或模板 Release。

## 使用

1. 将 Release 插件包中的 `addons/godot-minigame/` 放入 Godot 项目的 `addons/`。
2. 在编辑器中启用 Godot Minigame。
3. 配置模板分发源，或保留默认配置。
4. 在微信小游戏导出预设中选择自动匹配或指定模板版本。
5. 导出时插件下载并校验模板，然后生成小游戏工程。

## Catalog

两种产品的规范事实源：

- `product/plugin.json`：插件唯一版本和打包入口
- `catalog/plugin-stable.json`：已晋升插件 Release
- `catalog/templates.json`：已晋升模板及来源

`product/adapters.json` 只登记模板的适配来源和构建契约，不代表第三种产品。

插件更新和模板分发互不影响：设置页的“检查插件更新”只读取 `catalog/plugin-stable.json` 并下载 `plugin-v*` ZIP；模板选择只读取模板索引并下载 `<version>-<variant>-rN` TPZ。下载后的插件 ZIP 需要关闭 Godot 后安装，不会在编辑器运行时覆盖原生库。

`resources/versions.yaml` 是由 `catalog/templates.json` 生成的旧插件兼容投影，禁止手工双写：

```bash
python tools/product/product.py render-versions
python tools/product/product.py render-versions --check
```

## Release 命名

- 插件：`plugin-v1.0.4`
- 模板：`4.5.2-glx-2d-r1`（版本-变体-修订号，与产物文件名一致）

一个仓库只有一个 Release 列表和一个全局 latest，因此所有产品线都使用命名空间 tag 和独立 Catalog。

## 目录

- `src/`、`include/`：C++ 编辑器插件源码
- `addons/godot-minigame/`：可安装 addon 源码骨架
- `demo/`：示例项目，不作为插件源码或构建产物出口
- `product/`：产品定义与适配生产线注册
- `catalog/`：已验证发布物目录
- `resources/`：插件嵌入资源和兼容模板索引
- `templates/`：历史模板快照与可选模板组件
- `skills/`：Godot 微信适配移植技能
- `tools/product/`：验证、Promote 和插件打包工具
- `.github/workflows/`：产品、插件和适配自动化
- `dist/plugin/`、`dist/template/`：未跟踪的插件和模板临时产物出口

## License

MIT，见 `LICENSE`。
