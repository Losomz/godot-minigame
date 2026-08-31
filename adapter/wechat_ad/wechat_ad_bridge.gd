class_name WechatAdBridge
extends RefCounted

## Direct client for engine/wx-ad-bridge.js protocol v4.
## The caller owns ad unit IDs, placement, and all business decisions.

const REQUIRED_BRIDGE_VERSION := 4
const REWARDED_TIMEOUT_SECONDS := 120.0
const REQUIRED_BRIDGE_METHODS: Array[String] = [
	"dsWxAdGetBridgeVersion",
	"dsWxAdSetEventCallback",
	"dsWxAdClearEventCallback",
	"dsWxAdDebugState",
	"dsWxAdPrepareRewarded",
	"dsWxAdShowRewarded",
	"dsWxAdCancelRewarded",
	"dsWxAdShowInterstitial",
	"dsWxAdShowCustom",
	"dsWxAdHideCustom",
	"dsWxAdDestroyAll",
]
const REWARDED_FAILURE_STAGES: Array[String] = [
	"error",
	"api-missing",
	"create",
	"ad-unit-locked",
	"overlap",
]
const CUSTOM_POSITIONS: Array[String] = ["top", "bottom", "left", "right", "absolute"]

signal event_received(event: Dictionary)
signal ad_failed(event: Dictionary)
signal rewarded_completed(request_id: String, completed: bool)
signal interstitial_closed(ad_unit_id: String)
signal custom_loaded(ad_unit_id: String)
signal custom_hidden(ad_unit_id: String)

static var _active_instance: WechatAdBridge = null

var _is_ready := false
var _contract_established := false
var _owns_callback := false
var _godot_sdk: Object = null
var _event_callback: Object = null
var _request_serial := 0
var _active_rewarded_request_id := ""


## Establish the exact v4 method contract. An empty result means success.
func setup() -> String:
	if _is_ready:
		return ""
	if _active_instance != null and is_instance_valid(_active_instance) and _active_instance != self:
		return "another WechatAdBridge instance is already set up"
	if not ClassDB.class_exists("JavaScriptBridge"):
		return "JavaScriptBridge unavailable"
	_godot_sdk = JavaScriptBridge.get_interface("GODOTSDK")
	if _godot_sdk == null:
		return "GODOTSDK unavailable"
	var contract_error := _bridge_contract_error()
	if not contract_error.is_empty():
		_godot_sdk = null
		return contract_error
	_contract_established = true
	_event_callback = JavaScriptBridge.create_callback(Callable(self, "_on_javascript_event"))
	if not bool(_godot_sdk.dsWxAdSetEventCallback(_event_callback)):
		_event_callback = null
		_godot_sdk = null
		_contract_established = false
		return "dsWxAdSetEventCallback rejected"
	_owns_callback = true
	_is_ready = true
	_active_instance = self
	return ""


func is_ready() -> bool:
	return _is_ready


func prepare_rewarded(ad_unit_id: String) -> Dictionary:
	var error := _validate_ad_request(ad_unit_id)
	if not error.is_empty():
		return _failure_result(error)
	_godot_sdk.dsWxAdPrepareRewarded(ad_unit_id.strip_edges())
	return _success_result()


func show_rewarded(ad_unit_id: String) -> Dictionary:
	var error := _validate_ad_request(ad_unit_id)
	if not error.is_empty():
		return _failure_result(error)
	if not _active_rewarded_request_id.is_empty():
		return _failure_result("another rewarded request is still active")
	_request_serial += 1
	var request_id := str(_request_serial)
	_active_rewarded_request_id = request_id
	_start_rewarded_timeout(request_id)
	if not bool(_godot_sdk.dsWxAdShowRewarded(ad_unit_id.strip_edges(), request_id)):
		_finish_rewarded(request_id, false)
		return _failure_result("dsWxAdShowRewarded rejected the request")
	return {"success": true, "error": "", "request_id": request_id}


## Cancellation unbinds the consumer. The JS singleton remains occupied until wx
## closes it or the pending show chain fails, so a late close cannot hit a new request.
func cancel_rewarded() -> void:
	var request_id := _active_rewarded_request_id
	_active_rewarded_request_id = ""
	if not request_id.is_empty() and _is_ready:
		_godot_sdk.dsWxAdCancelRewarded(request_id)


func show_interstitial(ad_unit_id: String) -> Dictionary:
	var error := _validate_ad_request(ad_unit_id)
	if not error.is_empty():
		return _failure_result(error)
	_godot_sdk.dsWxAdShowInterstitial(ad_unit_id.strip_edges())
	return _success_result()


func show_custom(ad_unit_id: String, placement: Dictionary) -> Dictionary:
	var error := _validate_ad_request(ad_unit_id)
	if not error.is_empty():
		return _failure_result(error)
	var normalized := _normalize_custom_placement(placement)
	if not bool(normalized.get("success", false)):
		return _failure_result(str(normalized.get("error", "invalid custom placement")))
	var value: Dictionary = normalized["value"]
	_godot_sdk.dsWxAdShowCustom(
		ad_unit_id.strip_edges(),
		value["position"],
		value["width"],
		value["estimated_height"],
		value["offset_x"],
		value["offset_y"],
		value["left"],
		value["top"]
	)
	return _success_result()


## Empty ad_unit_id hides every cached custom-ad slot.
func hide_custom(ad_unit_id: String = "") -> Dictionary:
	var error := _validate_bridge()
	if not error.is_empty():
		return _failure_result(error)
	var normalized := ad_unit_id.strip_edges()
	if not normalized.is_empty() and not normalized.begins_with("adunit-"):
		return _failure_result("invalid WeChat ad unit id: %s" % normalized)
	_godot_sdk.dsWxAdHideCustom(normalized)
	return _success_result()


func destroy_all() -> Dictionary:
	var error := _validate_bridge()
	if not error.is_empty():
		return _failure_result(error)
	cancel_rewarded()
	_godot_sdk.dsWxAdDestroyAll()
	return _success_result()


func debug_state() -> Dictionary:
	var error := _validate_bridge()
	if not error.is_empty():
		return _failure_result(error)
	_godot_sdk.dsWxAdDebugState()
	return _success_result()


## Remote cleanup is only legal after the complete v4 contract and callback were established.
func dispose() -> void:
	if _contract_established and _owns_callback and _godot_sdk != null:
		cancel_rewarded()
		_godot_sdk.dsWxAdDestroyAll()
		_godot_sdk.dsWxAdClearEventCallback()
	if _active_instance == self:
		_active_instance = null
	_event_callback = null
	_godot_sdk = null
	_active_rewarded_request_id = ""
	_owns_callback = false
	_contract_established = false
	_is_ready = false


func _on_javascript_event(args: Array) -> void:
	var event := _parse_event(args)
	if event.is_empty():
		var invalid_event := {
			"type": "bridge",
			"stage": "parse",
			"ok": false,
			"errMsg": "invalid JSON event payload",
		}
		event_received.emit(invalid_event)
		ad_failed.emit(invalid_event)
		return
	event_received.emit(event)
	var ok := bool(event.get("ok", false))
	if not ok:
		ad_failed.emit(event)
	var ad_type := str(event.get("type", ""))
	var stage := str(event.get("stage", ""))
	if ad_type == "rewarded" and (stage == "close" or (not ok and stage in REWARDED_FAILURE_STAGES)):
		var request_id := str(event.get("requestId", ""))
		if request_id.is_empty() or request_id != _active_rewarded_request_id:
			return
		_finish_rewarded(
			request_id,
			ok and stage == "close" and bool(event.get("isEnded", false))
		)
	elif ad_type == "interstitial" and ok and stage == "close":
		interstitial_closed.emit(str(event.get("adUnitId", "")))
	elif ad_type == "custom" and ok and stage == "load":
		custom_loaded.emit(str(event.get("adUnitId", "")))
	elif ad_type == "custom" and ok and stage == "hide":
		custom_hidden.emit(str(event.get("adUnitId", "")))


func _start_rewarded_timeout(request_id: String) -> void:
	var scene_tree := Engine.get_main_loop() as SceneTree
	if scene_tree == null:
		return
	var timer := scene_tree.create_timer(REWARDED_TIMEOUT_SECONDS)
	timer.timeout.connect(Callable(self, "_on_rewarded_timeout").bind(request_id), CONNECT_ONE_SHOT)


func _on_rewarded_timeout(request_id: String) -> void:
	if request_id.is_empty() or request_id != _active_rewarded_request_id:
		return
	var event := {
		"type": "rewarded",
		"stage": "timeout",
		"ok": false,
		"requestId": request_id,
		"errMsg": "rewarded request timed out after 120 seconds",
	}
	event_received.emit(event)
	ad_failed.emit(event)
	_active_rewarded_request_id = ""
	if _is_ready:
		_godot_sdk.dsWxAdCancelRewarded(request_id)
	rewarded_completed.emit(request_id, false)


func _finish_rewarded(request_id: String, completed: bool) -> void:
	if request_id.is_empty() or request_id != _active_rewarded_request_id:
		return
	_active_rewarded_request_id = ""
	rewarded_completed.emit(request_id, completed)


func _bridge_contract_error() -> String:
	if _godot_sdk == null:
		return "GODOTSDK unavailable"
	for method_name in REQUIRED_BRIDGE_METHODS:
		if _godot_sdk.get(method_name) == null:
			return "bridge method missing: %s" % method_name
	var version := int(_godot_sdk.dsWxAdGetBridgeVersion())
	if version != REQUIRED_BRIDGE_VERSION:
		return "bridge contract v%d required, found v%d" % [REQUIRED_BRIDGE_VERSION, version]
	return ""


func _validate_bridge() -> String:
	if not _is_ready or not _contract_established or _godot_sdk == null:
		return "WeChat ad bridge not ready"
	return ""


func _validate_ad_request(ad_unit_id: String) -> String:
	var error := _validate_bridge()
	if not error.is_empty():
		return error
	var normalized := ad_unit_id.strip_edges()
	if normalized.is_empty() or not normalized.begins_with("adunit-"):
		return "invalid WeChat ad unit id: %s" % normalized
	return ""


func _normalize_custom_placement(placement: Dictionary) -> Dictionary:
	var position_value: Variant = placement.get("position", "top")
	if not position_value is String:
		return _placement_failure("position must be a String")
	var position := str(position_value).to_lower()
	if position not in CUSTOM_POSITIONS:
		return _placement_failure("unsupported custom position: %s" % position)
	var defaults := {
		"width": 350.0,
		"estimated_height": 120.0,
		"offset_x": 0.0,
		"offset_y": 0.0,
		"left": 0.0,
		"top": 0.0,
	}
	var values := {}
	for key in defaults:
		var raw: Variant = placement.get(key, defaults[key])
		if typeof(raw) not in [TYPE_INT, TYPE_FLOAT]:
			return _placement_failure("%s must be numeric" % key)
		var number := float(raw)
		if not is_finite(number):
			return _placement_failure("%s must be finite" % key)
		values[key] = number
	if values["width"] <= 0.0 or values["estimated_height"] <= 0.0:
		return _placement_failure("custom width and estimated_height must be greater than zero")
	if position == "absolute" and (not placement.has("left") or not placement.has("top")):
		return _placement_failure("absolute custom placement requires left and top")
	values["position"] = position
	return {"success": true, "error": "", "value": values}


func _parse_event(args: Array) -> Dictionary:
	if args.is_empty():
		return {}
	var payload: Variant = args[0]
	if payload is Array:
		var wrapped := payload as Array
		if wrapped.is_empty():
			return {}
		payload = wrapped[0]
	if payload is Dictionary:
		return payload as Dictionary
	var parsed: Variant = JSON.parse_string(str(payload))
	return parsed as Dictionary if parsed is Dictionary else {}


func _success_result() -> Dictionary:
	return {"success": true, "error": ""}


func _failure_result(error: String) -> Dictionary:
	return {"success": false, "error": error}


func _placement_failure(error: String) -> Dictionary:
	return {"success": false, "error": error, "value": {}}
