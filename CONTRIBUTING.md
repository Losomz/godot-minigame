# 贡献指南

## 分支选择

- 插件、Catalog、公共胶水和产品流程的临时分支直接从 `develop` 创建，PR 回 `develop`。
- Godot 版本适配从对应版本分支开功能分支，例如 `feature/4.5-*`，不要合并到产品 `main`。
- `upstream-sync` 只允许 fast-forward 同步上游，禁止加入自有提交。
- `main` 只接收验证后的产品变更和 Promote PR。

`feature/*`、`fix/*` 和 `refactor/*` 都是一次性工作分支，不是 `develop` 与其他开发分支之间的长期中间层。

## Issue 信息

请提供：

- Godot 版本
- 插件版本
- 模板 Release tag
- 操作系统与架构
- 复现步骤、日志和必要截图

下载或 Release 问题还应提供分发源、资产文件名和 SHA-256。

## 开发环境

- Godot 4.4+
- Python 3.11+
- SCons
- C++17 编译器

```bash
git submodule update --init --recursive
```

## 插件构建

```bash
scons platform=windows arch=x86_64 target=template_release embed_resources=yes
scons platform=linux arch=x86_64 target=template_release embed_resources=yes
scons platform=macos arch=universal target=template_release embed_resources=yes
```

插件源码骨架位于 `addons/godot-minigame/`。原生库统一生成到 `dist/plugin/native/<platform>/`，可安装 ZIP 和 staging 位于 `dist/plugin/`；模板候选产物位于 `dist/template/`。整个 `dist/` 都是可删除、可重建且不进入 Git 的临时出口。

## 必须验证

涉及插件或模板的产品定义、版本、Catalog、模板索引或打包逻辑时运行：

```bash
python tools/product/product.py validate
python tools/product/product.py render-versions --check
python -m unittest discover -s tests/product -v
```

涉及 C++ 时至少构建一个目标平台。涉及适配时还必须执行对应分支声明的构建和真机验证流程。

## Release 与 Promote

- 插件只能通过 `plugin-v*` tag 从产品主线发布。
- 插件和模板 Release 不得覆盖已有 tag 或资产。
- `resources/versions.yaml` 由 `catalog/templates.json` 生成，不能直接编辑。
- 适配分支不能直接合入 `main`；使用 `promote-template.yml` 创建只修改 Catalog 和生成索引的 PR。
- 不提交 `dist/`、`.tpz`、插件二进制或其他 Release 产物。`templates/` 下已审核的历史模板快照除外。

完整流程见 `docs/branching-and-releases.md`。

## PR 要求

PR 描述必须说明：

- 修改动机
- 行为与兼容性变化
- 影响的产品：plugin 或 template
- 执行过的验证命令
- 涉及 Release 时的来源 commit、tag 和 SHA-256
