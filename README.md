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
- 设置页统一管理模板选择、下载、覆盖和清理
- 模板缓存跨项目共享，下载后无需重启即可导出
- 插件与模板独立版本和更新通道
- 可选微信小游戏广告组件
- 版本分支内可复现的 Godot 微信适配补丁与验证 Skill

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
  --bundle-template /path/to/minigame4.5.2-glx-2d-r1.tpz
```

TPZ 在插件 ZIP 中只保存一份。模板版本与自定义 TPZ 均在插件设置页选择，不再写入各个导出预设。下载的模板保存在 Godot 编辑器全局缓存中，所有项目共享；设置页可以重新下载覆盖、删除当前缓存或清空全部模板缓存。缓存变更立即生效，不需要重启 Godot。

## 使用

1. 将 Release 插件包中的 `addons/godot-minigame/` 放入 Godot 项目的 `addons/`。
2. 在编辑器中启用 Godot Minigame。
3. 在插件设置页配置模板分发源并选择模板版本，或填写自定义 TPZ 直链。
4. 在设置页下载模板；需要时可以覆盖下载、删除当前缓存或清空全部缓存。
5. 创建微信小游戏导出预设并直接导出。导出只使用设置页当前选中的已缓存或内置模板，不会临时下载模板。

## 插件日常调试

`demo/` 是最小 Godot 插件测试项目。插件源码仍只有根目录下的 `addons/godot-minigame/` 一份；本地脚本会把它作为目录 junction 挂载到 demo，并将刚编译的 Windows DLL 放入 addon 的忽略目录：

```powershell
pwsh tools/dev/build_demo.ps1
```

随后直接用 Godot 打开 `demo/project.godot`。修改 C++ 后重新运行脚本即可，不需要执行插件 ZIP 打包或发布 CI。

## Catalog

两种产品的规范事实源：

- `product/plugin.json`：插件唯一版本和打包入口
- `catalog/plugin-stable.json`：已晋升插件 Release
- `catalog/templates.json`：已晋升模板及来源

`product/adapters.json` 只登记模板的适配来源和构建契约，不代表第三种产品。

插件更新和模板分发互不影响：设置页的“检查插件更新”只读取 `catalog/plugin-stable.json` 并下载 `plugin-v*` ZIP；模板选择只读取模板索引并下载 `<version>-<variant>-rN` TPZ。插件更新下载完成后停在“等待确认安装”，不会静默关闭编辑器。用户点击“安装并重启”后，插件先保存场景和项目设置，再由外部辅助程序等待 Godot 退出、替换完整插件目录并重新打开当前项目；安装失败会回滚旧插件。

`resources/versions.yaml` 仅是模板发布流程由 `catalog/templates.json` 生成的投影文件；插件运行时只读取 JSON Catalog，禁止手工双写：

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
- `demo/`：最小插件测试项目，不保存 addon 副本或导出产物
- `product/`：产品定义与适配生产线注册
- `catalog/`：已验证发布物目录
- `resources/`：插件嵌入资源和模板索引
- `src/templates/`：插件运行时模板目录与缓存管理实现
- `tools/product/`：验证、Promote 和插件打包工具
- `.github/workflows/`：产品、插件和适配自动化
- `dist/plugin/`、`dist/template/`：未跟踪的插件和模板临时产物出口

版本专用内容只存在于 `4.5`、`4.6` 等适配分支，并统一放在 `.agent/skills/godot-wechat-minigame-adapter/`、`adapter/` 和 `godot/`。这些目录不会从适配分支合回 `develop` 或 `main`。

## License

MIT，见 `LICENSE`。
