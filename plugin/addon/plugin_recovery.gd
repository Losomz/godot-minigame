@tool
extends Node

signal status_changed(message: String)
signal failed(message: String)

const ADDON_PREFIX := "addons/godot-minigame/"
const PRODUCT_ID := "godot-minigame"
const RELEASE_MANIFEST_URL := "https://github.com/Losomz/godot-minigame/releases/download/plugin-v%s/plugin-update.json"

var expected_version := ""
var editor_interface
var cache_root := ""
var incoming_package_path := ""
var package_path := ""
var expected_package_sha256 := ""
var manifest_request: HTTPRequest
var package_request: HTTPRequest


func start(version: String, target_editor_interface) -> void:
	if is_instance_valid(manifest_request) or is_instance_valid(package_request):
		return
	expected_version = version.strip_edges()
	editor_interface = target_editor_interface
	cache_root = _cache_root_for_version(expected_version)
	DirAccess.make_dir_recursive_absolute(cache_root)
	status_changed.emit("正在获取 Godot Minigame %s 的正式发布清单..." % expected_version)

	manifest_request = HTTPRequest.new()
	add_child(manifest_request)
	manifest_request.request_completed.connect(_on_manifest_completed)
	var override_url := OS.get_environment("GODOT_MINIGAME_PLUGIN_UPDATE_URL").strip_edges()
	var manifest_url := override_url if not override_url.is_empty() else RELEASE_MANIFEST_URL % expected_version
	var request_error := manifest_request.request(manifest_url)
	if request_error != OK:
		_fail("无法启动发布清单请求，错误码：%d" % request_error)


static func has_attempt_marker(version: String) -> bool:
	return FileAccess.file_exists(_attempt_marker_path(version))


static func clear_attempt_marker(version: String) -> void:
	var marker := _attempt_marker_path(version)
	if FileAccess.file_exists(marker):
		DirAccess.remove_absolute(marker)


static func _cache_root_for_version(version: String) -> String:
	var project_path := ProjectSettings.globalize_path("res://").trim_suffix("/")
	var project_id := project_path.sha256_text().substr(0, 16)
	return OS.get_cache_dir().path_join("godot-minigame/plugin-recovery").path_join(project_id).path_join(version)


static func _attempt_marker_path(version: String) -> String:
	return _cache_root_for_version(version).path_join("restart-attempt.json")


func _on_manifest_completed(result: int, response_code: int, _headers: PackedStringArray, body: PackedByteArray) -> void:
	manifest_request.queue_free()
	manifest_request = null
	if result != HTTPRequest.RESULT_SUCCESS or response_code != 200:
		_fail("发布清单下载失败，HTTP %d。" % response_code)
		return

	var parsed = JSON.parse_string(body.get_string_from_utf8())
	if not parsed is Dictionary:
		_fail("发布清单不是有效 JSON。")
		return
	var manifest: Dictionary = parsed
	if int(manifest.get("schema_version", 0)) != 1 or String(manifest.get("product", "")) != PRODUCT_ID:
		_fail("发布清单产品信息无效。")
		return
	if not bool(manifest.get("published", false)) or String(manifest.get("version", "")) != expected_version:
		_fail("发布清单版本与当前插件声明不一致。")
		return
	if String(manifest.get("tag", "")) != "plugin-v%s" % expected_version:
		_fail("发布清单标签无效。")
		return

	var platforms = manifest.get("platforms", {})
	var platform_key := _platform_manifest_key()
	if not platforms is Dictionary or not platforms.has(platform_key):
		_fail("发布清单不支持当前平台：%s。" % platform_key)
		return
	var platform_data = platforms[platform_key]
	if not platform_data is Dictionary:
		_fail("当前平台发布信息无效。")
		return
	var asset_url := String(platform_data.get("url", "")).strip_edges()
	expected_package_sha256 = String(platform_data.get("sha256", "")).strip_edges().to_lower()
	var asset_name := String(platform_data.get("asset", "")).get_file()
	if not asset_url.begins_with("https://") or not asset_name.ends_with(".zip") or not _is_sha256(expected_package_sha256):
		_fail("当前平台发布地址或校验值无效。")
		return

	incoming_package_path = cache_root.path_join(asset_name + ".incoming")
	package_path = cache_root.path_join(asset_name)
	DirAccess.remove_absolute(incoming_package_path)
	status_changed.emit("正在下载并校验 Godot Minigame %s..." % expected_version)
	package_request = HTTPRequest.new()
	add_child(package_request)
	package_request.download_file = incoming_package_path
	package_request.request_completed.connect(_on_package_completed)
	var request_error := package_request.request(asset_url)
	if request_error != OK:
		_fail("无法启动插件包下载，错误码：%d" % request_error)


func _on_package_completed(result: int, response_code: int, _headers: PackedStringArray, _body: PackedByteArray) -> void:
	package_request.queue_free()
	package_request = null
	if result != HTTPRequest.RESULT_SUCCESS or response_code != 200 or not FileAccess.file_exists(incoming_package_path):
		_fail("插件包下载失败，HTTP %d。" % response_code)
		return
	if FileAccess.get_sha256(incoming_package_path).to_lower() != expected_package_sha256:
		DirAccess.remove_absolute(incoming_package_path)
		_fail("插件包 SHA-256 校验失败。")
		return
	DirAccess.remove_absolute(package_path)
	if DirAccess.rename_absolute(incoming_package_path, package_path) != OK:
		_fail("无法保存已校验的插件包。")
		return

	status_changed.emit("正在验证插件内容并准备安全重启...")
	var stage_result := _extract_and_validate_package(package_path)
	if not bool(stage_result.get("success", false)):
		_fail(String(stage_result.get("error", "插件包验证失败。")))
		return
	var launch_error := await _launch_installer(String(stage_result["staged_addon"]))
	if not launch_error.is_empty():
		_fail(launch_error)


func _extract_and_validate_package(path: String) -> Dictionary:
	var zip := ZIPReader.new()
	if zip.open(path) != OK:
		return {"success": false, "error": "插件包无法作为 ZIP 打开。"}

	var stage_root := cache_root.path_join("staged")
	var staged_addon := stage_root.path_join(ADDON_PREFIX.trim_suffix("/"))
	var remove_error := _remove_tree(stage_root)
	if remove_error != OK:
		zip.close()
		return {"success": false, "error": "无法清理旧的修复暂存目录。"}
	DirAccess.make_dir_recursive_absolute(staged_addon)

	var seen := {}
	for archive_path in zip.get_files():
		var is_directory := archive_path.ends_with("/")
		var canonical := archive_path.trim_suffix("/") if is_directory else archive_path
		if not canonical.begins_with(ADDON_PREFIX) or "\\" in canonical:
			zip.close()
			_remove_tree(stage_root)
			return {"success": false, "error": "插件包包含越界路径：%s" % archive_path}
		var relative := canonical.trim_prefix(ADDON_PREFIX)
		if relative.is_empty() or not _safe_relative_path(relative):
			zip.close()
			_remove_tree(stage_root)
			return {"success": false, "error": "插件包包含不安全路径：%s" % archive_path}
		var key := relative.to_lower()
		if seen.has(key):
			zip.close()
			_remove_tree(stage_root)
			return {"success": false, "error": "插件包包含重复路径：%s" % archive_path}
		seen[key] = true
		var destination := staged_addon.path_join(relative)
		if is_directory:
			if DirAccess.make_dir_recursive_absolute(destination) != OK:
				zip.close()
				_remove_tree(stage_root)
				return {"success": false, "error": "无法创建插件目录：%s" % relative}
			continue
		if DirAccess.make_dir_recursive_absolute(destination.get_base_dir()) != OK:
			zip.close()
			_remove_tree(stage_root)
			return {"success": false, "error": "无法创建插件文件目录：%s" % relative}
		var file := FileAccess.open(destination, FileAccess.WRITE)
		if file == null:
			zip.close()
			_remove_tree(stage_root)
			return {"success": false, "error": "无法写入插件文件：%s" % relative}
		file.store_buffer(zip.read_file(archive_path))
		file.close()
	zip.close()

	var required := PackedStringArray([
		"plugin.cfg",
		"plugin.gd",
		"plugin_recovery.gd",
		"godot-minigame.gdextension",
		"update_helper.gd",
		_waiter_relative_path(),
	])
	for relative in required:
		if relative.is_empty() or not FileAccess.file_exists(staged_addon.path_join(relative)):
			_remove_tree(stage_root)
			return {"success": false, "error": "插件包缺少关键文件：%s" % relative}

	var config := ConfigFile.new()
	if config.load(staged_addon.path_join("plugin.cfg")) != OK or String(config.get_value("plugin", "version", "")) != expected_version:
		_remove_tree(stage_root)
		return {"success": false, "error": "插件包版本与恢复目标不一致。"}
	var descriptor := ConfigFile.new()
	if descriptor.load(staged_addon.path_join("godot-minigame.gdextension")) != OK:
		_remove_tree(stage_root)
		return {"success": false, "error": "插件包描述文件无效。"}
	var library_reference := String(descriptor.get_value("libraries", _platform_library_key(), "")).trim_prefix("./")
	if not _safe_relative_path(library_reference) or not FileAccess.file_exists(staged_addon.path_join(library_reference)):
		_remove_tree(stage_root)
		return {"success": false, "error": "插件包缺少当前平台原生库。"}
	return {"success": true, "staged_addon": staged_addon}


func _launch_installer(staged_addon: String) -> String:
	var helper_root := cache_root.path_join("helper")
	var install_root := cache_root.path_join("install")
	if _remove_tree(helper_root) != OK or _remove_tree(install_root) != OK:
		return "无法清理旧的插件安装事务。"
	DirAccess.make_dir_recursive_absolute(helper_root)
	DirAccess.make_dir_recursive_absolute(install_root)

	var helper_script := helper_root.path_join("update_helper.gd")
	var waiter_path := helper_root.path_join(_waiter_relative_path().get_file())
	if DirAccess.copy_absolute(staged_addon.path_join("update_helper.gd"), helper_script) != OK:
		return "无法准备插件更新脚本。"
	if DirAccess.copy_absolute(staged_addon.path_join(_waiter_relative_path()), waiter_path) != OK:
		return "无法准备插件更新等待器。"
	if OS.get_name() != "Windows" and FileAccess.set_unix_permissions(waiter_path, 493) != OK:
		return "无法设置插件更新等待器权限。"
	if not _write_text(helper_root.path_join("project.godot"), "[application]\nconfig/name=\"Godot Minigame Recovery\"\n"):
		return "无法创建插件恢复工程。"

	var project_path := ProjectSettings.globalize_path("res://").trim_suffix("/")
	var result_path := cache_root.path_join("last-install.json")
	var manifest := {
		"project_path": project_path,
		"addon_path": ProjectSettings.globalize_path("res://addons/godot-minigame").trim_suffix("/"),
		"staged_addon_path": staged_addon,
		"install_root": install_root,
		"result_path": result_path,
		"editor_path": OS.get_executable_path(),
		"version": expected_version,
		"relaunch": true,
	}
	var manifest_path := helper_root.path_join("manifest.json")
	if not _write_text(manifest_path, JSON.stringify(manifest, "  ")):
		return "无法写入插件恢复事务。"
	if editor_interface:
		editor_interface.save_all_scenes()
	ProjectSettings.save()

	var marker_path := _attempt_marker_path(expected_version)
	DirAccess.make_dir_recursive_absolute(marker_path.get_base_dir())
	if not _write_text(marker_path, JSON.stringify({"version": expected_version, "started_at": Time.get_unix_time_from_system()})):
		return "无法记录插件恢复状态。"
	var ready_path := helper_root.path_join("waiter.ready")
	DirAccess.remove_absolute(ready_path)
	var args := PackedStringArray([
		str(OS.get_process_id()),
		ready_path,
		OS.get_executable_path(),
		helper_root,
		helper_script,
		manifest_path,
		result_path,
		expected_version,
	])
	var waiter_pid := OS.create_process(waiter_path, args, false)
	if waiter_pid <= 0:
		clear_attempt_marker(expected_version)
		return "无法启动插件更新等待器。"

	for _attempt in range(100):
		if FileAccess.file_exists(ready_path):
			status_changed.emit("插件包已验证，Godot 将自动重启并完成修复。")
			get_tree().quit()
			return ""
		if not OS.is_process_running(waiter_pid):
			break
		await get_tree().create_timer(0.05).timeout
	OS.kill(waiter_pid)
	clear_attempt_marker(expected_version)
	return "插件更新等待器未能接管当前 Godot 进程。"


func _platform_manifest_key() -> String:
	match OS.get_name():
		"Windows":
			return "windows-%s" % Engine.get_architecture_name()
		"Linux":
			return "linux-%s" % Engine.get_architecture_name()
		"macOS":
			return "macos-universal"
	return ""


func _platform_library_key() -> String:
	match OS.get_name():
		"Windows":
			return "windows.x86_64"
		"Linux":
			return "linux.x86_64"
		"macOS":
			return "macos"
	return ""


func _waiter_relative_path() -> String:
	match OS.get_name():
		"Windows":
			return "bin/windows/godot-minigame-update-waiter.windows.%s.exe" % Engine.get_architecture_name()
		"Linux":
			return "bin/linux/godot-minigame-update-waiter.linux.%s" % Engine.get_architecture_name()
		"macOS":
			return "bin/macos/godot-minigame-update-waiter.macos"
	return ""


func _safe_relative_path(path: String) -> bool:
	if path.is_empty() or path.is_absolute_path() or ":" in path or "\\" in path:
		return false
	for part in path.split("/"):
		if part.is_empty() or part == "." or part == "..":
			return false
	return true


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if character not in "0123456789abcdef":
			return false
	return true


func _write_text(path: String, content: String) -> bool:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(content)
	file.close()
	return true


func _remove_tree(path: String) -> int:
	if not DirAccess.dir_exists_absolute(path):
		return OK
	var dir := DirAccess.open(path)
	if dir == null:
		return ERR_CANT_OPEN
	dir.include_hidden = true
	dir.list_dir_begin()
	var name := dir.get_next()
	while not name.is_empty():
		if name != "." and name != "..":
			var child := path.path_join(name)
			var error := _remove_tree(child) if dir.current_is_dir() else DirAccess.remove_absolute(child)
			if error != OK:
				dir.list_dir_end()
				return error
		name = dir.get_next()
	dir.list_dir_end()
	return DirAccess.remove_absolute(path)


func _fail(message: String) -> void:
	if is_instance_valid(manifest_request):
		manifest_request.queue_free()
		manifest_request = null
	if is_instance_valid(package_request):
		package_request.queue_free()
		package_request = null
	failed.emit(message)
