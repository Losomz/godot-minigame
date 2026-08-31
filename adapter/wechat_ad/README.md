# 微信小游戏广告桥 v4

这是 Godot 4.5.x + Godot Minigame 1.0.4 的框架无关广告组件，支持激励视频、插屏和原生模板广告。Banner 当前未实现。

v4 是破坏性替换：旧 `WechatAd` Node、Inspector 广告位和 v2/v3 兼容接口已删除。业务直接持有 `WechatAdBridge`，广告位和展示策略由业务传入。

## 文件与加载顺序

```text
adapter/wechat_ad/
├── wechat_ad_bridge.gd
└── engine/
    ├── game.js
    └── wx-ad-bridge.js
```

微信导出产物必须按以下顺序启动：

```text
godot-sdk -> godot -> wx-ad-bridge -> GODOTSDK.startGame()
```

`wx-ad-bridge.js` 只向已经存在的 `GODOTSDK` 挂载接口。缺少真实 `GODOTSDK` 时会拒绝挂载，不创建 Mock，也不静默降级。

## Godot 直接 API

将 `wechat_ad_bridge.gd` 复制到项目并长期持有一个实例：

```gdscript
extends Node

var wechat_ads := WechatAdBridge.new()


func _ready() -> void:
	var setup_error := wechat_ads.setup()
	if not setup_error.is_empty():
		push_warning("微信广告不可用: %s" % setup_error)
		return
	wechat_ads.ad_failed.connect(_on_ad_failed)
	wechat_ads.rewarded_completed.connect(_on_rewarded_completed)
	wechat_ads.custom_hidden.connect(_on_custom_hidden)
	wechat_ads.prepare_rewarded("adunit-rewarded")


func watch_rewarded() -> void:
	var result := wechat_ads.show_rewarded("adunit-rewarded")
	if not result.success:
		push_warning(result.error)


func _on_rewarded_completed(_request_id: String, completed: bool) -> void:
	if completed:
		grant_reward()


func _on_ad_failed(event: Dictionary) -> void:
	push_warning("广告失败: %s" % JSON.stringify(event))


func _on_custom_hidden(ad_unit_id: String) -> void:
	print("原生模板已关闭: %s" % ad_unit_id)


func _exit_tree() -> void:
	wechat_ads.dispose()
```

同一进程只能有一个成功 `setup()` 的实例，避免后注册者覆盖唯一 JS 回调。`setup()` 返回空字符串表示 v4 版本和完整方法表均已确认；失败实例的 `dispose()` 不会调用未知 JS 方法。

公开方法：

```text
setup() -> String
is_ready() -> bool
prepare_rewarded(ad_unit_id) -> Dictionary
show_rewarded(ad_unit_id) -> Dictionary
cancel_rewarded() -> void
show_interstitial(ad_unit_id) -> Dictionary
show_custom(ad_unit_id, placement) -> Dictionary
hide_custom(ad_unit_id = "") -> Dictionary
destroy_all() -> Dictionary
debug_state() -> Dictionary
dispose() -> void
```

公开信号：

```text
event_received(event)
ad_failed(event)
rewarded_completed(request_id, completed)
interstitial_closed(ad_unit_id)
custom_loaded(ad_unit_id)
custom_hidden(ad_unit_id)
```

方法返回的 `success=true` 只表示请求已交给 JS。展示失败通过 `ad_failed` 返回；激励奖励只能依据 `rewarded_completed(..., true)` 发放。

## 原生模板位置

```gdscript
wechat_ads.show_custom("adunit-custom", {
	"position": "bottom",
	"width": 320.0,
	"estimated_height": 100.0,
	"offset_x": 0.0,
	"offset_y": -16.0,
})
```

`position` 支持 `top/bottom/left/right/absolute`。`absolute` 必须同时提供 `left` 和 `top`。宽高必须大于零，所有数值必须有限；无效显式配置直接返回错误，不会悄悄替换成默认值。坐标使用微信逻辑窗口坐标，不乘设备像素比，并在可见窗口内收敛。

## 生命周期语义

- 激励视频是进程级单例，第一次成功创建后锁定广告位且永不销毁。重叠展示会被拒绝；只有激励视频保留一次官方 `show -> load -> show` 兜底。
- 激励请求固定 120 秒超时。超时或取消只解绑业务 requestId，微信展示仍占用槽位，直至真实 close 或最终 show 失败；这样迟到 close 不会结算下一次请求。
- 插屏每次请求创建一个新实例，严格 `load -> show`，close、onError、load/show 拒绝都会只结算一次并销毁该实例。活动请求期间拒绝重叠展示。
- 原生模板按 `adUnitId + style` 缓存；样式变化销毁旧槽位。`show()`、`hide()` Promise 都会被等待并上报拒绝，不使用激励视频的 load 兜底。
- 原生模板同时监听 `onHide` 和 `onClose`，两者由同一槽位去重；用户从微信原生界面关闭后也会发出 `custom_hidden(ad_unit_id)`。

## 改动点与理由

| 改动 | 理由 |
| --- | --- |
| 协议升级到 v4，严格校验 SDK、版本和方法表 | 旧桥会伪造 `GODOTSDK` 并报告挂载成功，实际调用直到运行时才失败 |
| 删除旧 Node/Inspector 兼容 API | 当前仓库无内部消费者，继续维护两套入口只会掩盖迁移错误 |
| 激励请求与微信展示占用分离 | 修复并发覆盖 requestId、超时后迟到 close 串到新请求 |
| 所有激励终态错误携带输入 requestId | 修复 API 缺失/创建失败只能等待 120 秒的问题 |
| 插屏回调捕获具体实例并 settle-once | 修复旧 close 销毁新实例、错误路径泄漏实例的问题 |
| Custom 等待 show/hide Promise 并监听原生关闭 | 修复 hide 拒绝未处理、用户关闭后业务仍认为广告活动的问题 |
| 只为激励保留一次 load 重试 | 这是唯一有官方展示语义依据的兜底，避免对其他广告格式做自动重试 |
| `--ad` 校验并记录入口、桥版本和 SHA-256 | 确保 build 产物实际包含经过审查的 v4，而不是只根据文件名判断“已融合” |

## build 产物

```powershell
python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --revision 1 --ad
```

带 `--ad` 的 TPZ 必须包含：

```text
engine/game.js
engine/wx-ad-bridge.js
```

打包器会在复制前校验桥声明为 v4，并校验 `engine/game.js` 的加载顺序。`BUILD_INFO.md` 会记录 `Ad merged: true`、协议 `v4` 和两个文件的 SHA-256；外部 `.sha256.txt` 同样包含这两个文件。不带 `--ad` 的产物不增加这些文件或元数据。

## 验证

```powershell
node --check adapter/wechat_ad/engine/wx-ad-bridge.js
```

真机仍需使用有效 AppID/广告位确认填充、完整观看和用户关闭行为。

## Rush 项目后续同步清单

本仓库不会修改 `RushSpecialForces`。迁移时需要原子替换 JS 与 GDScript 为 v4，并同时完成：

1. 将 120 秒超时统一交给 `WechatAdBridge`，删除业务层重复 watchdog。
2. 修正同步收到 `completed=false` 时展示函数仍返回 true 的路径。
3. 将 `ad_failed` 和 `custom_hidden(ad_unit_id)` 映射到业务活动状态清理。
4. 重新打包并确认 build 元数据为 `Ad merged: true`、协议 v4、哈希与实际文件一致。
