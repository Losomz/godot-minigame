### Unreleased

*   **重构**: 模具化整理——`templates/` 变为打包基底库（格式文件 + 裁切模板 `configs/` + 登记表 `manifest.json`）；历史模板快照、旧 tpz、`demo/wxgame` 导出产物清除；广告组件迁入 `adapter/wechat_ad/`；`adapter/configs/` 裁切模板迁至 `templates/configs/`。
*   **功能**: 统一打包入口 `ci/package.py`——`--template`（基底）/`--variant glx|webgl`/`--profile`（裁切简名或路径）/`--exceptions`/`--ad`（广告融合）/`--revision`/`--out`；`--list` 提供模板与裁切模板选择；产物命名 `minigame{版本}-{变体}-{裁切}[-ad][-noexc]-r{N}`，统一落 `dist/`（5 件套）。
*   **功能**: 裁切清单与异常开关参数化——`package_wechat_glx_template.py` 新增 `--profile`（裁切清单，`templates/configs/*.py`）与 `--exceptions enabled|disabled`；`detect.py` 新增 `wechat_glx_exceptions=yes|no`（子模块 `08024e25`）；CI `build-wechat-glx.yml` 新增 `profile`/`exceptions` 输入。`disabled` 产物约 4.91 MiB（省 1.14 MiB，GLX 库抛异常时 abort，测试用 `enabled`）。
*   **调研**: 实锤 GLX 构建包体差异来源——C++ 异常支持占约 1.14 MiB（GLX+异常 6.05 MiB vs GLX 无异常 4.91 MiB vs 非 GLX 4.85 MiB），GLX 静态库本身仅约 +67 KB；数据已写入 `adapter/WECHAT_GLX.md` 与 `templates/configs/wechat_2d.py`。
*   **重构**: 删除与 `templates/` 重复的 `demo/template/` 模板副本；统一各版本模板内 `使用前阅读.md` / `小游戏工程说明.md`；`.agent/skills` 改为引用 `adapter/` 文档（删除重复的 wechat-glx.md）。
*   **重构**: CI 基建从适配包剥离——`build_wechat_glx.ps1` 与构建依赖清单移至仓库级 `ci/`；workflow 版本常量改从 `adapter/patches/manifest.json` 读取，删除与 `verify_source()` 重复的校验步骤。

### 1.0.1 (2025-09-21)

*   **修复**: 修复了网络请求中的时序问题和竞争条件。
*   **修复**: 调整了更新检查逻辑，以正确显示“已是最新版本”状态。
*   **功能**: 完善了 `latest.json` 的结构，增加了 `changelog` 和多平台支持。