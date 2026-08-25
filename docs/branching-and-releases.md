# 分支与模板发布

## 分支职责

- `main`：Losomz 仓库的默认主分支，只保存产品主线、中央 Actions workflow 和发布规则。
- `vendor/godothub-main`：`godothub/godot-minigame:main` 的只读镜像，不从该分支发布模板。
- `4.5`：Godot 4.5.2 微信 GLX 正式构建分支。
- `feature/4.5-*`：从 `4.5` 派生的裁切或功能候选分支。

不要直接将 `godothub:main` 合并到产品 `main`。先更新 `vendor/godothub-main`，再通过独立 PR 审核并挑选需要进入 `main` 的改动。

## 构建契约

中央 workflow 固定从 `main` 运行，`source_ref` 指定实际构建的远端分支。可构建的 4.5 分支必须包含：

- `adapter/ci/requirements-build.txt`
- `adapter/configs/wechat_2d.py`
- `adapter/patches/manifest.json`
- `adapter/scripts/build_wechat_glx.ps1`
- `adapter/scripts/package_wechat_glx_template.py`
- 与 manifest 中 `wechat_glx_ref` 一致的 `godot` gitlink

Godot base commit 和 GLX commit 从所选分支的 manifest 读取。打包流程不运行 `adapter/tests/`、Godot runtime test 或任何 `test_*.js`。

## 手动构建

仅生成有 14 天保留期的 Actions Artifact：

```bash
gh workflow run build-wechat-glx.yml \
  --repo Losomz/godot-minigame \
  --ref main \
  -f source_ref=4.5 \
  -f revision=1 \
  -f release_mode=artifact-only
```

生成 Prerelease：

```bash
gh workflow run build-wechat-glx.yml \
  --repo Losomz/godot-minigame \
  --ref main \
  -f source_ref=4.5 \
  -f revision=1 \
  -f release_mode=prerelease \
  -f expected_tpz_sha256=<optional-local-sha256>
```

`expected_tpz_sha256` 可选。提供后，CI 产物必须与本地 clean build 完全一致；不匹配时保留诊断 Artifact、阻止 Release 并让运行失败。不提供时，Prerelease 使用并记录 CI 产物哈希。

## Release Tag

- 正式 `4.5` 分支：`4.5.2`
- 其他 4.5 分支：`4.5.2-branch-<branch-slug>-r<revision>-<commit-sha7>`

Release 和 Tag 均不可覆盖。功能分支每个 commit 使用独立 Tag。Stable 只能由已经完成同包真机验证的 Prerelease 原地提升，不能重新构建或替换 TPZ。
