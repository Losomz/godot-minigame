class_name WechatAd
extends Node

## 文件用途：Godot 微信小游戏广告组件。
## 放置位置：复制到目标 Godot 项目的 res:// 目录，并挂载到一个 Node。
## 配置方式：在 Inspector 中填写微信后台创建的广告位 ID 和原生模板样式。
## 运行条件：微信导出目录的 engine/game.js 必须在启动 Godot 前加载配套广告桥。
##
## 本组件不依赖任何业务框架。它只负责调用 JavaScript 广告桥，并把微信回调
## 转换成 Godot 信号。奖励发放、界面提示等业务逻辑应由使用方自行处理。

signal rewarded_closed(completed: bool)
signal interstitial_closed
signal custom_loaded
signal ad_error(ad_type: String, message: String)

# 微信广告桥只维护一个 Godot 回调，因此同一时间只允许一个 WechatAd 实例占用它。
static var _active_instance: WechatAd = null

# ===== 微信广告配置开始 =====
# 广告位 ID 必须来自当前小游戏 AppID，并与广告类型一一对应。
@export_group("微信广告位")
@export var rewarded_ad_unit_id: String = ""
@export var interstitial_ad_unit_id: String = ""
@export var custom_ad_unit_id: String = ""

# 原生模板广告使用微信屏幕坐标，不是 Godot 场景节点坐标。
# position 为 top/bottom/left/right 时由桥按窗口自动居中，absolute 时才使用 left/top。
@export_group("原生模板样式")
@export_enum("top", "bottom", "left", "right", "absolute") var custom_position: String = "top"
@export var custom_width: float = 350.0
@export var custom_estimated_height: float = 120.0
@export var custom_offset_x: float = 0.0
@export var custom_offset_y: float = 0.0
@export var custom_left: float = 0.0
@export var custom_top: float = 0.0
# ===== 微信广告配置结束 =====

var _godot_sdk: Object = null
# JavaScriptBridge 回调必须由成员变量持有，否则可能被回收并停止接收微信事件。
var _event_callback: Object = null
# 激励视频是否还在等待终态事件；v2 桥的 close/cancel/失败都可能成为终态，防止重复发信号。
var _rewarded_pending := false


func _enter_tree() -> void:
	_claim_instance()


func _ready() -> void:
	if _active_instance == self:
		initialize()


func _exit_tree() -> void:
	if _active_instance != self:
		return
	# 场景销毁时清空 JS 回调，避免微信事件继续调用已经释放的 Godot 对象。
	if _has_bridge_method("dsWxAdSetEventCallback"):
		_godot_sdk.dsWxAdSetEventCallback(null)
	_event_callback = null
	_rewarded_pending = false
	_godot_sdk = null
	_active_instance = null


## 初始化 JavaScript 桥。
## 返回 true 表示 GODOTSDK 和广告桥已经可用；本地编辑器中返回 false 属于正常情况。
func initialize() -> bool:
	if not _claim_instance():
		return false
	if is_available():
		return true
	if not OS.has_feature("web") or not ClassDB.class_exists("JavaScriptBridge"):
		return false

	_godot_sdk = JavaScriptBridge.get_interface("GODOTSDK")
	if not _has_bridge_method("dsWxAdSetEventCallback"):
		_godot_sdk = null
		return false

	_event_callback = JavaScriptBridge.create_callback(Callable(self, "_on_javascript_event"))
	var registered: Variant = _godot_sdk.dsWxAdSetEventCallback(_event_callback)
	if not bool(registered):
		_event_callback = null
		return false
	return true


## 返回当前是否运行在已正确加载广告桥的微信小游戏环境中。
func is_available() -> bool:
	return _godot_sdk != null \
		and _event_callback != null \
		and _has_bridge_method("dsWxAdSetEventCallback")


## 请求展示激励视频。
## 返回 true 只表示请求已经传给 JavaScript，不表示广告展示成功或用户完整观看。
## 最终观看结果通过 rewarded_closed(completed) 返回，只有 completed=true 才能发奖励。
func show_rewarded() -> bool:
	if not _can_show("rewarded", "dsWxAdShowRewarded", rewarded_ad_unit_id):
		return false
	_rewarded_pending = true
	return bool(_godot_sdk.dsWxAdShowRewarded(rewarded_ad_unit_id.strip_edges()))


## 请求展示插屏广告。
## 返回 true 只表示请求已发出；用户关闭广告后会触发 interstitial_closed 信号。
func show_interstitial() -> bool:
	if not _can_show("interstitial", "dsWxAdShowInterstitial", interstitial_ad_unit_id):
		return false
	return bool(_godot_sdk.dsWxAdShowInterstitial(interstitial_ad_unit_id.strip_edges()))


## 请求展示原生模板广告。
## position 为 top/bottom/left/right 时桥按窗口自动居中，absolute 时使用 left/top，
## 全部位置都会再叠加 offset；修改 Inspector 配置后无需改 JS 桥。
func show_custom() -> bool:
	if not _can_show("custom", "dsWxAdShowCustom", custom_ad_unit_id):
		return false
	return bool(_godot_sdk.dsWxAdShowCustom(
		custom_ad_unit_id.strip_edges(),
		custom_position,
		maxf(1.0, custom_width),
		maxf(1.0, custom_estimated_height),
		custom_offset_x,
		custom_offset_y,
		custom_left,
		custom_top
	))


## 隐藏原生模板广告；广告位留空时隐藏全部原生模板。
## 激励视频和插屏由微信界面负责关闭，因此不提供隐藏方法。
func hide_custom() -> bool:
	if not is_available() and not initialize():
		return _fail("custom", "微信广告桥不可用，请确认导出入口已加载 wx-ad-bridge.js")
	if not _has_bridge_method("dsWxAdHideCustom"):
		return _fail("custom", "广告桥缺少 dsWxAdHideCustom 方法")
	return bool(_godot_sdk.dsWxAdHideCustom(custom_ad_unit_id.strip_edges()))


# JS 桥统一传入 JSON 字符串，避免普通 JavaScript 对象跨桥时丢失字段。
func _on_javascript_event(args: Array) -> void:
	var event_data: Dictionary = _parse_event(args)
	if event_data.is_empty():
		_fail("unknown", "收到无法解析的微信广告事件")
		return

	var ad_type: String = str(event_data.get("type", "unknown"))
	var event_name: String = str(event_data.get("stage", "unknown"))

	match [ad_type, event_name]:
		["rewarded", "close"]:
			# isEnded=true 才表示完整观看，业务奖励只能在这个条件下发放。
			_finish_rewarded(bool(event_data.get("isEnded", false)))
		["rewarded", "cancel"]:
			# v2 桥的取消请求事件，等同于未完成观看。
			_finish_rewarded(false)
		["interstitial", "close"]:
			interstitial_closed.emit()
		["custom", "load"]:
			custom_loaded.emit()

	# v2 桥把创建/展示失败也作为激励终态上报，避免业务一直等待。
	if ad_type == "rewarded" and not bool(event_data.get("ok", true)):
		_finish_rewarded(false)

	if not bool(event_data.get("ok", true)):
		var message: String = str(event_data.get("errMsg", "未知广告错误"))
		ad_error.emit(ad_type, message)


func _finish_rewarded(completed: bool) -> void:
	if not _rewarded_pending:
		return
	_rewarded_pending = false
	rewarded_closed.emit(completed)


func _parse_event(args: Array) -> Dictionary:
	if args.is_empty():
		return {}

	var payload: Variant = args[0]
	# 某些 JavaScriptBridge 版本会把真实参数再包一层数组，这里同时兼容两种形态。
	if payload is Array:
		var wrapped_payload: Array = payload as Array
		if wrapped_payload.is_empty():
			return {}
		payload = wrapped_payload[0]
	if payload is Dictionary:
		return payload as Dictionary

	var parsed: Variant = JSON.parse_string(str(payload))
	if parsed is Dictionary:
		return parsed as Dictionary
	return {}


func _can_show(ad_type: String, method_name: String, ad_unit_id: String) -> bool:
	# 节点可能早于广告桥初始化完成，调用时再尝试初始化一次。
	if not is_available() and not initialize():
		return _fail(ad_type, "微信广告桥不可用，请确认导出入口已加载 wx-ad-bridge.js")
	if not _has_bridge_method(method_name):
		return _fail(ad_type, "广告桥缺少 %s 方法" % method_name)

	var normalized_id: String = ad_unit_id.strip_edges()
	if normalized_id.is_empty() or not normalized_id.begins_with("adunit-"):
		return _fail(ad_type, "广告位 ID 无效：%s" % normalized_id)
	return true


func _claim_instance() -> bool:
	if _active_instance != null \
		and is_instance_valid(_active_instance) \
		and _active_instance != self:
		push_warning("[WechatAd] 已存在另一个 WechatAd 节点；全项目只能保留一个实例")
		return false
	_active_instance = self
	return true


func _has_bridge_method(method_name: String) -> bool:
	return _godot_sdk != null and _godot_sdk[method_name] != null


func _fail(ad_type: String, message: String) -> bool:
	push_warning("[WechatAd] type=%s message=%s" % [ad_type, message])
	ad_error.emit(ad_type, message)
	return false
