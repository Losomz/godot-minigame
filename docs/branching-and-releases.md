# 分支与发布

## 分支职责

- `upstream-sync`：上游只读同步线。
- `develop`：插件和公共适配基线的日常开发线。
- `main`：验证后的稳定产品主线和发布控制面。
- `4.5`、`4.6` 等版本分支：从产品基线演进对应 Godot 版本适配。

版本适配分支定期合入 `develop` 的公共变化，禁止反向合入 `develop/main`。插件和公共结构变更先进入 `develop`，验证后再进入 `main`。

## 产品控制面

- `plugin/plugin.json`：插件身份和版本
- `plugin/catalog/plugin-stable.json`：稳定插件 Release
- `plugin/catalog/templates.json`：已晋升模板 Release
- `plugin/resources/versions.yaml`：模板目录生成投影
- `adapter/adapters.json`：版本适配分支和构建契约

`plugin/resources/versions.yaml` 由模板 Catalog 生成，不允许手工双写。

## 插件发布

插件使用 `plugin-v<semver>` tag。`release-plugin.yml` 从 `main` 构建各平台原生库，组装标准 addon ZIP，创建 Release，然后将更新清单晋升到 `plugin/catalog/plugin-stable.json`。

## 模板发布

模板使用 `<godot-version>-<variant>-rN` tag。中央 `wechat-template.yml` 从 `adapter/adapters.json` 解析登记分支和构建契约，生成不可变 TPZ；完成设备验证后，将 Release 信息晋升到模板 Catalog。

Promote 只允许修改：

- `plugin/catalog/templates.json`
- `plugin/resources/versions.yaml`

已发布 tag、Release 资产、来源 commit 和 SHA-256 不得覆盖或复用。

## Latest 徽章归属

仓库 Latest 徽章(GitHub Releases 页的 "Latest" 标记)只属于插件 release,作为对外更新标记:

- 插件发布(`plugin-v<semver>`)创建 Release 时显式认领 Latest,CI 发布后校验归属。
- 模板发布(`<godot-version>-<variant>-rN`)一律不认领 Latest(`make_latest=false`,正式版也不例外),CI 发布完成后强制让出并校验;校验失败会中止模板目录自动上架。
- 在 GitHub UI 手动把 prerelease 转为正式版会抢占 Latest,需要手动把 Latest 指回最新插件版。
