# 分支与发布

## 分支职责

- `upstream-sync`：`citizenll/godot-minigame:main` 的只读镜像，只允许 fast-forward。
- `main`：产品主线，保存插件、公共胶水、Catalog、中央 workflow 和发布规则。
- `develop`：日常产品开发线，插件和控制面功能先在这里验证。
- `4.5`：Godot 4.5.2 适配生产线。
- `4.6` 等后续版本分支：对应 Godot 版本的模板适配生产线。

上游变更先进入 `upstream-sync`，再经过独立 PR 选择性移植。不要直接 merge `upstream-sync` 到 `main` 或 `develop`。

插件、Catalog 和公共 workflow 的临时分支直接从 `develop` 创建并合回 `develop`；验证完成后再由 `develop` 进入 `main`。`feature/*`、`fix/*` 和 `refactor/*` 都是一次性工作分支，不构成第五层分支。

仓库只发布两种产品：

- 插件：从产品主线发布 `plugin-v*` Release。
- 模板：从版本适配分支生产 `<godot>-<variant>-rN` Release（如 `4.5.2-glx-2d-r1`），再通过 Promote 登记到 `main`。

## 控制面与生产线

`main` 是产品控制面，版本适配分支是模板生产线。适配源码和完整模板不会通过 merge 回归主线：

```text
adapter branch commit
  -> central build workflow
  -> immutable <godot>-<variant>-rN Release
  -> device verification
  -> promote step in wechat-template.yml
  -> catalog commit to main
```

Promote PR 只允许修改：

- `catalog/templates.json`
- `resources/versions.yaml`

## 模板来源契约

`product/adapters.json` 只是模板来源登记表，不是第三种产品。每条模板生产线登记其 Godot 版本、适配分支、构建 workflow 和必需路径。当前 `4.5` 必须提供：

- `.gitmodules`
- `godot` gitlink
- `adapter/patches/manifest.json`
- `adapter/scripts/package_wechat_glx_template.py`
- `ci/build_wechat_glx.ps1`
- `ci/requirements-build.txt`
- `templates/configs/wechat_2d.py`

Manifest 必须锁定官方 Godot base commit、适配 commit 和工具链版本。中央 workflow 使用远端分支的精确 commit 构建并记录 SHA-256。临时适配分支只用于 Artifact 验证；正式 `4.5.2-glx-2d-r*` Release 只从登记的 `4.5` 分支创建。

## 插件发布

插件版本唯一来源是 `product/plugin.json`，根 `plugin.cfg` 和 addon `plugin.cfg` 必须一致。

```text
plugin-v<semver>
```

`release-plugin.yml` 在 Actions 页面手动触发（只允许 `main`），版本号唯一来源是 `product/plugin.json`；workflow 校验 `plugin-v<version>` Release/tag 尚不存在，构建 Windows/Linux/macOS 原生库，组装全平台 addon ZIP，生成 SHA-256 和 `plugin-update.json`，自动创建 tag 与 Release。

Release 验证完成后，将 `plugin-update.json` 内容晋升到 `catalog/plugin-stable.json`。插件只读取该固定 Catalog，不使用仓库全局 latest。

## 模板发布

中央 `wechat-template.yml` 从指定适配分支构建：

```bash
gh workflow run wechat-template.yml \
  --repo Losomz/godot-minigame \
  --ref main \
  -f mode=build \
  -f source_ref=4.5 \
  -f revision=2 \
  -f release_mode=prerelease
```

正式命名（版本-变体-修订号，由产物文件名 `minigame<...>.tpz` 去掉 `minigame` 前缀得到）：

```text
4.5.2-glx-2d-r2
```

临时适配分支只生成 Actions Artifact，不创建正式模板 Release，也不进入 Catalog。

## 晋升模板

1. 对 Release 中的同一 `.tpz` 完成真机验证。
2. 将 GitHub Prerelease 原地提升为稳定 Release，不重新构建或替换资产。
3. 从 `main` 运行 `wechat-template.yml` 的 promote 模式，输入 Release tag、适配分支和完整 source commit。
4. Workflow 验证 Release 状态、tag target、来源提交属于登记的适配分支历史、资产数量和 SHA-256。
5. Workflow 运行 `product.py promote-template` 并把 `catalog/templates.json` 与 `resources/versions.yaml` 直接提交到 `main`。

## 不可变规则

- 禁止覆盖或删除已发布的 `plugin-v*` 和模板 `*-rN` tag。
- 禁止替换 Release 资产。
- 正式产物必须绑定精确 source commit 和 SHA-256。
- `resources/versions.yaml` 禁止手工修改。
- 一个模板 revision 只能递增，不能回退或复用。
