@tool
extends EditorPlugin

const ADDON_ROOT := "res://addons/godot-minigame"
const PluginRecovery = preload("res://addons/godot-minigame/plugin_recovery.gd")

var dock
var wechat_platform
var wechat_platform_added := false
var recovery
var repair_dialog: AcceptDialog
var expected_version := ""


func _enter_tree() -> void:
	_initialize_plugin.call_deferred()


func _exit_tree() -> void:
	_unregister_wechat_platform()
	if is_instance_valid(dock):
		dock.queue_free()
	if is_instance_valid(recovery):
		recovery.queue_free()
	if is_instance_valid(repair_dialog):
		repair_dialog.queue_free()


func _initialize_plugin() -> void:
	var check := _check_installation()
	expected_version = String(check.get("version", ""))
	if not bool(check.get("valid", false)):
		_start_recovery(String(check.get("error", "插件安装不完整。")))
		return

	PluginRecovery.clear_attempt_marker(expected_version)
	dock = _instantiate_dock()
	if dock:
		add_control_to_bottom_panel(dock, "Minigame")
	_register_wechat_platform()


func _check_installation() -> Dictionary:
	var config := ConfigFile.new()
	if config.load(ADDON_ROOT.path_join("plugin.cfg")) != OK:
		return {"valid": false, "error": "无法读取 plugin.cfg。", "version": ""}

	var version := String(config.get_value("plugin", "version", "")).strip_edges()
	if version.is_empty():
		return {"valid": false, "error": "plugin.cfg 未声明插件版本。", "version": ""}

	var descriptor := ConfigFile.new()
	var descriptor_path := ADDON_ROOT.path_join("godot-minigame.gdextension")
	if descriptor.load(descriptor_path) != OK:
		return {"valid": false, "error": "无法读取 GDExtension 描述文件。", "version": version}

	var library_key := _platform_library_key()
	if library_key.is_empty():
		return {"valid": false, "error": "当前平台不受插件支持：%s" % OS.get_name(), "version": version}
	var library_reference := String(descriptor.get_value("libraries", library_key, "")).strip_edges()
	if library_reference.is_empty():
		return {"valid": false, "error": "描述文件缺少当前平台原生库。", "version": version}
	var library_path := ADDON_ROOT.path_join(library_reference.trim_prefix("./")).simplify_path()
	if not FileAccess.file_exists(library_path):
		return {"valid": false, "error": "插件原生库缺失：%s" % library_path, "version": version}

	if not Engine.has_singleton("PluginUpdateManager"):
		return {"valid": false, "error": "插件原生库未加载。", "version": version}
	var manager = Engine.get_singleton("PluginUpdateManager")
	if not manager.has_method("get_runtime_version"):
		return {"valid": false, "error": "插件脚本与原生库版本不兼容。", "version": version}
	var runtime_version := String(manager.get_runtime_version()).strip_edges()
	if runtime_version != version:
		return {
			"valid": false,
			"error": "插件版本混装：配置为 %s，已加载原生库为 %s。" % [version, runtime_version],
			"version": version,
		}
	if not ClassDB.class_exists("GodotMinigameDock") or not ClassDB.class_exists("WeChatExportPlatform"):
		return {"valid": false, "error": "插件原生类型注册不完整。", "version": version}

	return {"valid": true, "version": version}


func _platform_library_key() -> String:
	match OS.get_name():
		"Windows":
			return "windows.x86_64"
		"Linux":
			return "linux.x86_64"
		"macOS":
			return "macos"
	return ""


func _start_recovery(reason: String, manual_retry := false) -> void:
	_unregister_wechat_platform()
	_show_repair_dialog("检测到插件安装异常：\n%s" % reason, false)
	if expected_version.is_empty():
		_show_repair_dialog("无法确定需要恢复的插件版本，请重新安装正式插件包。", true)
		return

	if not is_instance_valid(recovery):
		recovery = PluginRecovery.new()
		add_child(recovery)
		recovery.status_changed.connect(_on_recovery_status_changed)
		recovery.failed.connect(_on_recovery_failed)
	if manual_retry:
		PluginRecovery.clear_attempt_marker(expected_version)
	elif PluginRecovery.has_attempt_marker(expected_version):
		_show_repair_dialog("插件自动修复上次未能完成。请检查网络或文件占用后重试。", true)
		return

	recovery.start(expected_version, get_editor_interface())


func _show_repair_dialog(message: String, can_retry: bool) -> void:
	if not is_instance_valid(repair_dialog):
		repair_dialog = AcceptDialog.new()
		repair_dialog.title = "Godot Minigame 插件修复"
		repair_dialog.get_ok_button().text = "重试修复"
		repair_dialog.confirmed.connect(_on_repair_retry)
		add_child(repair_dialog)
	repair_dialog.dialog_text = message
	repair_dialog.get_ok_button().disabled = not can_retry
	if not repair_dialog.visible:
		repair_dialog.popup_centered(Vector2i(560, 220))


func _on_recovery_status_changed(message: String) -> void:
	_show_repair_dialog(message, false)


func _on_recovery_failed(message: String) -> void:
	push_error("Godot Minigame plugin recovery failed: %s" % message)
	_show_repair_dialog("插件自动修复失败：\n%s\n\n导出功能已停用。" % message, true)


func _on_repair_retry() -> void:
	_start_recovery("正在重新校验安装。", true)


func _instantiate_dock():
	const DOCK_CLASS_NAME := "GodotMinigameDock"
	if ClassDB.class_exists(DOCK_CLASS_NAME):
		var instance = ClassDB.instantiate(DOCK_CLASS_NAME)
		if instance is Control:
			return instance
		push_warning("Godot Minigame: %s is not a Control" % DOCK_CLASS_NAME)
		return null
	push_warning("Godot Minigame: %s class is not available" % DOCK_CLASS_NAME)
	return null


func _register_wechat_platform() -> void:
	if wechat_platform_added and wechat_platform and is_instance_valid(wechat_platform):
		return
	if not wechat_platform or not is_instance_valid(wechat_platform):
		wechat_platform = _instantiate_wechat_platform()
		if not wechat_platform:
			return
	add_export_platform(wechat_platform)
	wechat_platform_added = true


func _instantiate_wechat_platform():
	const PLATFORM_CLASS_NAME := "WeChatExportPlatform"
	if ClassDB.class_exists(PLATFORM_CLASS_NAME):
		var instance = ClassDB.instantiate(PLATFORM_CLASS_NAME)
		if instance:
			return instance
		push_warning("Godot Minigame: %s instantiation failed" % PLATFORM_CLASS_NAME)
		return null
	push_warning("Godot Minigame: %s class is not available" % PLATFORM_CLASS_NAME)
	return null


func _unregister_wechat_platform() -> void:
	if wechat_platform_added and wechat_platform and is_instance_valid(wechat_platform):
		remove_export_platform(wechat_platform)
	wechat_platform_added = false
	wechat_platform = null
