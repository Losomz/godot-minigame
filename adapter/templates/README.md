# 模板库（打包基底）

## 角色说明

**这里是打包基底（模具），不是成品模板库。**

- 每个版本目录（如 `4.5.2/`）只包含模板的**格式文件**：小游戏入口、配置、图片、说明文档、分包结构。
- 引擎产物（`godot.js`、`godot.wasm.br`）与运行壳（`godot-loader.js`、`godot-sdk.js`）**不在本目录**——由构建流程注入。
- **选择成品模板请到 `dist/`（本地产物）或 GitHub Release（正式分发）**。

## 目录结构

```
adapter/
  configs/             # 裁切参数模板库（可复制/继承复用）
    wechat_2d.py       # 2D 裁切模板（关闭 3D/XR 等）
  templates/
    manifest.json      # 基底登记表（统一打包入口读取）
    4.5.2/             # 4.5.2 基底（格式文件）
      game.js / game.json / project.config*.json
      images/ 使用前阅读.md 小游戏工程说明.md
      engine/game.js engine/demo-pck.bin
      subpack1/ weapp-adapter.js .eslintrc.js
```

## 打包（统一入口）

```bash
# 列出可用模板与裁切模板
python adapter/ci/package.py --list

# 打包 4.5.2 glx 2D 版（异常开启）
python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --exceptions enabled --revision 1

# 打包 4.5.2 glx 2D 版 + 广告融合 + 异常关闭
python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --exceptions disabled --revision 2 --ad

# 使用项目自己的裁切参数文件（任意路径）
python adapter/ci/package.py --template 4.5.2 --variant glx --profile ./my_project_trim.py --revision 1
```

产物命名：`minigame{版本}-{glx|webgl}-{裁切标签}[-ad][-noexc]-r{版本号}`，输出到 `dist/`。

## 裁切参数模板复用

- `adapter/configs/*.py` 每个文件 = 一份裁切参数（`disable_*` / `module_*` 布尔开关）。
- 复用方式：
  - 复制：`cp adapter/configs/wechat_2d.py adapter/configs/my_trim.py`，修改开关；
  - 继承：新文件里 `from wechat_2d import *` 后覆盖个别开关。
- 异常开关（`--exceptions`）与广告（`--ad`）是独立维度，不属于裁切参数文件。
