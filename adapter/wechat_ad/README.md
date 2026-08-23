# Godot 微信小游戏广告接入组件

这是一个不依赖业务框架的微信小游戏广告组件，适用于：

- Godot 4.5.x
- Godot Minigame 1.0.4
- 激励视频、插屏广告、原生模板广告

组件使用 Godot 官方 `JavaScriptBridge` 访问 Godot Minigame 提供的全局 `GODOTSDK`，再通过 `engine/wx-ad-bridge.js` 调用微信官方 `wx.create*Ad` API。

> 当前未实现 Banner。组件不包含 PowerShell、业务图片、AppID 或项目广告位。

打包时使用 `python ci/package.py --ad` 即可将本组件融合进模板（产物名带 `-ad`）。本目录是独立组件，也可以手动迁移到其他 Godot 项目。

## 1. 文件职责

```text
wechat_ad/
├── README.md
├── wechat_ad.gd
└── engine/
    ├── game.js
    └── wx-ad-bridge.js
```

| 文件 | 作用 | 使用位置 |
| --- | --- | --- |
| [`wechat_ad.gd`](./wechat_ad.gd) | Godot 广告节点，保存广告位、调用 JS 并把微信事件转换成信号 | 复制到目标 Godot 项目的 `res://` |
| [`engine/game.js`](./engine/game.js) | 完整 Godot 引擎入口，依次加载 `godot-sdk`、Godot 运行时、广告桥和 PCK | 覆盖微信导出目录的 `engine/game.js` |
| [`engine/wx-ad-bridge.js`](./engine/wx-ad-bridge.js) | 向 `GODOTSDK` 注入 `dsWxAd*` 方法并调用微信广告 API | 复制到微信导出目录的 `engine` |

根 `game.js` 不属于广告组件，也不需要修改。它继续负责微信小游戏外壳、加载界面和 `godot-loader`；广告只在 Godot 引擎分包中接入。

## 2. 调用链

```text
根 game.js
    -> godot-loader 加载 engine 分包

engine/game.js
    -> godot-sdk 创建全局 GODOTSDK
    -> godot 加载 Godot 运行时
    -> wx-ad-bridge 向 GODOTSDK 注入 dsWxAd* 方法
    -> GODOTSDK.startGame() 启动 Godot

GDScript
    -> JavaScriptBridge.get_interface("GODOTSDK")
    -> GODOTSDK.dsWxAdShow*
    -> wx.createRewardedVideoAd / createInterstitialAd / createCustomAd
    -> 微信回调
    -> JSON 字符串
    -> Godot 信号
```

`GODOTSDK` 只作为统一调用入口，真正的广告能力来自微信 `wx` API。

## 3. 接入 Godot

### 3.1 复制脚本

将：

```text
wechat_ad.gd
```

复制到目标项目，例如：

```text
res://scripts/platform/wechat_ad.gd
```

脚本继承普通 `Node`，不依赖任何业务框架：

- [打开 Godot 广告组件](./wechat_ad.gd#L1)

### 3.2 创建唯一广告节点

在不会随关卡切换销毁的启动场景中创建一个 Node，命名为 `WechatAd`，并挂载 `wechat_ad.gd`。

全项目同时只能存在一个 `WechatAd` 节点，因为 JS 桥只维护一个 Godot 事件回调。脚本会检测重复实例，后创建的节点不会覆盖正在工作的回调。

### 3.3 填写广告位

选中 `WechatAd` 节点，在 Inspector 中填写微信后台生成的广告位：

```text
Rewarded Ad Unit Id      激励视频广告位
Interstitial Ad Unit Id  插屏广告位
Custom Ad Unit Id        原生模板广告位
```

- [打开微信广告配置区](./wechat_ad.gd#L20)

广告位必须属于当前小游戏 AppID，并且类型必须对应：

```text
激励视频广告位 -> show_rewarded()
插屏广告位     -> show_interstitial()
原生模板广告位 -> show_custom()
```

原生模板的 `Custom Left`、`Custom Top`、`Custom Width` 使用微信屏幕坐标，不是 Godot Control 节点坐标。

### 3.4 连接信号并调用

```gdscript
extends Node

@onready var wechat_ad: WechatAd = $WechatAd


func _ready() -> void:
    wechat_ad.rewarded_closed.connect(_on_rewarded_closed)
    wechat_ad.interstitial_closed.connect(_on_interstitial_closed)
    wechat_ad.custom_loaded.connect(_on_custom_loaded)
    wechat_ad.ad_error.connect(_on_ad_error)


func show_rewarded_ad() -> void:
    wechat_ad.show_rewarded()


func _on_rewarded_closed(completed: bool) -> void:
    if completed:
        grant_reward()
    else:
        print("用户未完整观看，不发放奖励")


func _on_ad_error(ad_type: String, message: String) -> void:
    push_warning("广告失败 type=%s message=%s" % [ad_type, message])


func grant_reward() -> void:
    print("发放奖励")
```

公开接口：

- [初始化与 GODOTSDK 获取](./wechat_ad.gd#L61)
- [激励视频](./wechat_ad.gd#L92)
- [插屏广告](./wechat_ad.gd#L100)
- [原生模板广告](./wechat_ad.gd#L108)
- [隐藏原生模板](./wechat_ad.gd#L120)
- [微信事件处理](./wechat_ad.gd#L129)

`show_*()` 返回 `true` 只表示请求已经传给 JavaScript，不代表广告展示成功。激励奖励只能在 `rewarded_closed(true)` 时发放。

## 4. 覆盖微信导出文件

### 4.1 正常导出

使用 Godot Minigame 1.0.4 导出，并确保至少存在：

```text
<微信导出目录>/game.js
<微信导出目录>/godot-loader.js
<微信导出目录>/engine/game.js
<微信导出目录>/engine/godot-sdk.js
<微信导出目录>/engine/godot.js
<微信导出目录>/engine/demo-pck.bin
```

根 `game.js` 保持导出器生成的版本，不修改也不覆盖。

### 4.2 覆盖 engine

将组件目录中的：

```text
engine/
```

复制到微信导出目录的：

```text
<微信导出目录>/engine/
```

并覆盖同名 `game.js`。最终结构：

```text
<微信导出目录>/
├── game.js                     导出器原文件，不修改
├── godot-loader.js             导出器原文件
└── engine/
    ├── game.js                 使用组件完整覆盖
    ├── wx-ad-bridge.js         组件新增的唯一广告桥
    ├── godot-sdk.js            导出器原文件
    ├── godot.js                导出器原文件
    └── demo-pck.bin            Godot 游戏资源，不覆盖
```

组件的 `engine` 目录只包含入口和广告桥，不会覆盖 `godot-sdk.js`、Godot 运行时或 PCK。

### 4.3 PCK 名称

完整引擎入口默认读取：

```text
/engine/demo-pck.bin
```

如果导出文件名不同，修改 [`engine/game.js` 的 `packPath`](./engine/game.js#L27)。

## 5. engine/game.js 做了什么

[`engine/game.js`](./engine/game.js) 是完整可覆盖文件，不是代码片段。它负责：

1. 加载 `godot-sdk`，创建并暴露 `GODOTSDK`。
2. 加载 Godot 微信运行时。
3. 在 Godot 启动前加载唯一的广告桥。
4. 从微信文件系统读取 `demo-pck.bin`。
5. 将 PCK 复制到 Godot 虚拟文件系统。
6. 调用 `GODOTSDK.startGame()`。

广告桥加载顺序：

- [打开广告桥导入位置](./engine/game.js#L18)

PCK 读取和启动：

- [打开 `load_pack1`](./engine/game.js#L35)
- [打开 `startGame`](./engine/game.js#L43)

正确顺序必须是：

```text
godot-sdk -> godot -> wx-ad-bridge -> startGame
```

## 6. wx-ad-bridge.js 做了什么

[`engine/wx-ad-bridge.js`](./engine/wx-ad-bridge.js) 负责：

1. 从微信运行环境取得 `wx`。
2. 向 `GODOTSDK` 挂载 `dsWxAd*` 方法。
3. 创建并复用微信广告实例。
4. `show()` 失败时执行 `load()` 后再次展示。
5. 监听微信 `onClose`、`onLoad`、`onError`。
6. 将事件转换为 JSON 字符串回调 Godot。

关键位置：

- [取得微信 API](./engine/wx-ad-bridge.js#L19)
- [挂载 GODOTSDK 接口](./engine/wx-ad-bridge.js#L140)
- [整理并发送事件](./engine/wx-ad-bridge.js#L209)
- [展示失败后的加载兜底](./engine/wx-ad-bridge.js#L252)
- [激励视频实例](./engine/wx-ad-bridge.js#L299)
- [插屏广告实例](./engine/wx-ad-bridge.js#L345)
- [原生模板实例](./engine/wx-ad-bridge.js#L388)

桥接方法：

```text
dsWxAdSetEventCallback(callback)
dsWxAdDebugState()
dsWxAdShowRewarded(adUnitId)
dsWxAdShowInterstitial(adUnitId)
dsWxAdShowCustom(adUnitId, left, top, width)
dsWxAdHideCustom()
```

回调结构：

```json
{
  "type": "rewarded",
  "stage": "close",
  "ok": true,
  "adUnitId": "adunit-xxx",
  "isEnded": true
}
```

`errMsg` 和微信错误码是可选诊断字段；`isEnded` 只在激励视频关闭事件中出现。

## 7. 验证

在微信开发者工具中确认：

1. 根目录不存在广告桥，也没有广告 import。
2. `engine/wx-ad-bridge.js` 存在。
3. `engine/game.js` 在 `godot-sdk` 之后加载广告桥。
4. Godot 中 `WechatAd.is_available()` 返回 `true`。
5. 激励完整观看返回 `isEnded=true`，中途关闭返回 `false`。
6. 插屏关闭、原生模板加载和广告错误都能返回对应信号。

本地 Godot 编辑器没有微信 `wx` 环境，`is_available()` 返回 `false` 是正常行为。

## 8. 常见问题

### `GODOTSDK` 不存在

确认使用 Godot Minigame 微信导出，而不是普通 Godot Web 导出。

### `dsWxAd*` 方法不存在

确认 `engine/wx-ad-bridge.js` 存在，并已被 `engine/game.js` 在 `godot-sdk` 之后导入。

### 找不到 PCK

确认资源位于 `engine/demo-pck.bin`，或者同步修改 `packPath`。

### 广告位无效

广告位不能为空，必须以 `adunit-` 开头，并且属于当前小游戏 AppID。

### 激励视频什么时候发奖励

只能在 `rewarded_closed(true)` 时发放，不能根据按钮点击或 `show_rewarded()` 返回值发放。

## 9. 微信官方 API

- [wx.createRewardedVideoAd](https://developers.weixin.qq.com/minigame/dev/api/ad/wx.createRewardedVideoAd.html)
- [wx.createInterstitialAd](https://developers.weixin.qq.com/minigame/dev/api/ad/wx.createInterstitialAd.html)
- [wx.createCustomAd](https://developers.weixin.qq.com/minigame/dev/api/ad/wx.createCustomAd.html)
