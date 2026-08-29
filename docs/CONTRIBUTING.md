# 贡献指南

## 分支选择

- 插件、公共适配和仓库结构变更从 `develop` 创建分支并合回 `develop`。
- Godot 版本差异从对应适配分支创建功能分支。
- `main` 只接收验证后的产品变更和 Catalog Promote。
- `upstream-sync` 只允许同步上游提交。

## 开发验证

```bash
git submodule update --init --recursive
python tools/product/product.py validate
python tools/product/product.py render-versions --check
python -m unittest discover -s tests -v
node tests/adapter/test_godot_process_glx.js
node tests/adapter/test_min_runtime_loader.js
```

插件构建入口为 `plugin/build/SConstruct`，适配模板入口为 `adapter/ci/package.py`。所有生成内容进入 `dist/` 或被忽略的 `demo/addons/`，不得提交插件二进制、TPZ 或打包暂存目录。

本地验证插件替换时，先运行 `pwsh tools/dev/build_demo.ps1` 和 `python tools/product/product.py package-plugin`，再在 demo 的插件设置中选择“本地插件包”以及 `dist/plugin/godot-minigame-plugin-<version>.zip`。本地渠道直接替换，不要求版本递增；远端稳定版仍由正式 Catalog、Release 和 CI 驱动。

## PR 信息

说明修改动机、行为变化、影响的产品和执行过的验证。涉及 Release 时同时提供来源 commit、tag 和 SHA-256。
