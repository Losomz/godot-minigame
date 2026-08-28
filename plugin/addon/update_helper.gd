extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


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


func _copy_tree(source: String, destination: String) -> int:
	var source_dir := DirAccess.open(source)
	if source_dir == null:
		return ERR_CANT_OPEN
	source_dir.include_hidden = true
	var mkdir_error := DirAccess.make_dir_recursive_absolute(destination)
	if mkdir_error != OK:
		return mkdir_error
	source_dir.list_dir_begin()
	var name := source_dir.get_next()
	while not name.is_empty():
		if name != "." and name != "..":
			var source_path := source.path_join(name)
			var destination_path := destination.path_join(name)
			var error := _copy_tree(source_path, destination_path) if source_dir.current_is_dir() else source_dir.copy(source_path, destination_path)
			if error != OK:
				source_dir.list_dir_end()
				return error
		name = source_dir.get_next()
	source_dir.list_dir_end()
	return OK


func _find_temp_libraries(path: String, result: Array[String]) -> int:
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
			if dir.current_is_dir():
				var error := _find_temp_libraries(child, result)
				if error != OK:
					dir.list_dir_end()
					return error
			elif name.begins_with("~") and name.get_extension().to_lower() == "dll":
				result.append(child)
		name = dir.get_next()
	dir.list_dir_end()
	return OK


func _wait_for_temp_libraries(addon_path: String) -> Dictionary:
	var deadline := Time.get_ticks_msec() + 30000
	var locked_path := ""
	while true:
		var paths: Array[String] = []
		var scan_error := _find_temp_libraries(addon_path, paths)
		if scan_error != OK:
			return {"error": scan_error, "path": addon_path}
		locked_path = ""
		for path in paths:
			var remove_error := DirAccess.remove_absolute(path)
			if remove_error != OK and FileAccess.file_exists(path):
				locked_path = path
		if locked_path.is_empty():
			return {"error": OK, "path": ""}
		if Time.get_ticks_msec() >= deadline:
			return {"error": ERR_BUSY, "path": locked_path}
		await create_timer(0.2).timeout


func _write_result(path: String, success: bool, message: String, version: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({
			"success": success,
			"message": message,
			"version": version,
		}))


func _run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		quit(2)
		return
	var manifest_file := FileAccess.open(args[0], FileAccess.READ)
	if manifest_file == null:
		quit(3)
		return
	var parsed = JSON.parse_string(manifest_file.get_as_text())
	if not parsed is Dictionary:
		quit(4)
		return
	var manifest: Dictionary = parsed
	var addon_path := String(manifest.get("addon_path", ""))
	var staged_path := String(manifest.get("staged_addon_path", ""))
	var install_root := String(manifest.get("install_root", ""))
	var result_path := String(manifest.get("result_path", ""))
	var version := String(manifest.get("version", ""))
	var project_path := String(manifest.get("project_path", ""))
	var transaction_root := project_path.path_join(".godot/godot-minigame-update")
	var replacement_path := transaction_root.path_join("new")
	var backup_path := transaction_root.path_join("backup")

	var error := OK
	var existing_backup := DirAccess.dir_exists_absolute(backup_path)
	if existing_backup:
		error = ERR_ALREADY_EXISTS
	else:
		_remove_tree(transaction_root)
		error = _copy_tree(staged_path, replacement_path)

	var locked_path := ""
	if error == OK:
		var wait_result: Dictionary = await _wait_for_temp_libraries(addon_path)
		error = int(wait_result.get("error", FAILED))
		locked_path = String(wait_result.get("path", ""))

	var replacement_started := false
	if error == OK:
		error = DirAccess.rename_absolute(addon_path, backup_path)
		replacement_started = error == OK
	if error == OK:
		error = DirAccess.rename_absolute(replacement_path, addon_path)
	var rollback_error := OK
	if error != OK and replacement_started:
		if DirAccess.dir_exists_absolute(addon_path):
			rollback_error = ERR_ALREADY_EXISTS
		else:
			rollback_error = DirAccess.rename_absolute(backup_path, addon_path)

	var success := error == OK
	if success:
		_remove_tree(backup_path)
		_remove_tree(transaction_root)
		_remove_tree(install_root)
		_write_result(result_path, true, "Godot Minigame %s 更新完成" % version, version)
	else:
		var message := "插件更新失败，原插件未修改，错误码：%d" % error
		if error == ERR_BUSY and not locked_path.is_empty():
			message = "插件更新已取消：仍有 Godot 进程占用插件 DLL：%s" % locked_path
		elif error == ERR_ALREADY_EXISTS and existing_backup:
			message = "插件更新已取消：发现未完成更新的备份：%s" % backup_path
		if replacement_started and rollback_error == OK:
			message = "插件更新失败并已回滚，错误码：%d" % error
		if rollback_error != OK:
			message = "插件更新失败且回滚失败，安装错误码：%d，回滚错误码：%d，备份：%s" % [error, rollback_error, backup_path]
		elif not existing_backup:
			_remove_tree(transaction_root)
			_remove_tree(install_root)
		_write_result(result_path, false, message, version)

	if bool(manifest.get("relaunch", true)):
		var editor_path := String(manifest.get("editor_path", ""))
		OS.create_process(editor_path, PackedStringArray(["--editor", "--path", project_path]))
	quit(0 if success else 1)
