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

## PR 信息

说明修改动机、行为变化、影响的产品和执行过的验证。涉及 Release 时同时提供来源 commit、tag 和 SHA-256。
