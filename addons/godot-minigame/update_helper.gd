extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _remove_tree(path: String) -> int:
	if not DirAccess.dir_exists_absolute(path):
		return OK
	var dir := DirAccess.open(path)
	if dir == null:
		return ERR_CANT_OPEN
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
	var parent_pid := int(manifest.get("parent_pid", 0))
	while parent_pid > 0 and OS.is_process_running(parent_pid):
		await create_timer(0.2).timeout

	var addon_path := String(manifest.get("addon_path", ""))
	var staged_path := String(manifest.get("staged_addon_path", ""))
	var result_path := String(manifest.get("result_path", ""))
	var version := String(manifest.get("version", ""))
	var replacement_path := addon_path + ".godot-minigame-new"
	var backup_path := addon_path + ".godot-minigame-backup"
	_remove_tree(replacement_path)
	_remove_tree(backup_path)

	var error := _copy_tree(staged_path, replacement_path)
	if error == OK:
		error = DirAccess.rename_absolute(addon_path, backup_path)
	if error == OK:
		error = DirAccess.rename_absolute(replacement_path, addon_path)
	var rollback_error := OK
	if error != OK and DirAccess.dir_exists_absolute(backup_path):
		rollback_error = DirAccess.rename_absolute(backup_path, addon_path)
		if rollback_error != OK:
			rollback_error = _copy_tree(backup_path, addon_path)

	var success := error == OK
	if success:
		_remove_tree(backup_path)
		_write_result(result_path, true, "Godot Minigame %s 更新完成" % version, version)
	else:
		_remove_tree(replacement_path)
		var message := "插件更新失败并已回滚，错误码：%d" % error
		if rollback_error != OK:
			message = "插件更新失败且回滚失败，安装错误码：%d，回滚错误码：%d" % [error, rollback_error]
		_write_result(result_path, false, message, version)

	if bool(manifest.get("relaunch", true)):
		var editor_path := String(manifest.get("editor_path", ""))
		var project_path := String(manifest.get("project_path", ""))
		OS.create_process(editor_path, PackedStringArray(["--editor", "--path", project_path]))
	quit(0 if success else 1)
