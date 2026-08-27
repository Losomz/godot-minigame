extends SceneTree


func _initialize() -> void:
	call_deferred("_run")


func _fail(message: String) -> void:
	print("LOCAL_PACKAGE_TEST_FAILED: ", message)
	push_error(message)


func _run() -> void:
	if not Engine.has_singleton("PluginUpdateManager"):
		_fail("PluginUpdateManager singleton is unavailable")
		return
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		_fail("Expected one fixture manifest path")
		return
	var manifest_file := FileAccess.open(args[0], FileAccess.READ)
	if manifest_file == null:
		_fail("Cannot read fixture manifest")
		return
	var cases = JSON.parse_string(manifest_file.get_as_text())
	if not cases is Array:
		_fail("Fixture manifest is invalid")
		return

	var manager = Engine.get_singleton("PluginUpdateManager")
	for case in cases:
		var package_path: String = case["path"]
		var source_hash := FileAccess.get_sha256(package_path)
		var result: Dictionary = manager.select_local_package(package_path, "1.0.9")
		if result.get("success", false) != case["valid"]:
			_fail("Unexpected validation result for %s: %s" % [case["name"], result])
			return
		if not case["valid"]:
			continue
		if result["version"] != case["version"] or manager.get_current_state() != 4:
			_fail("Valid package did not enter downloaded state: %s" % case["name"])
			return
		if FileAccess.get_sha256(package_path) != source_hash:
			_fail("Source package was modified: %s" % case["name"])
			return
		var cached_path: String = manager.get_download_file_path()
		if not FileAccess.file_exists(cached_path) or FileAccess.get_sha256(cached_path) != source_hash:
			_fail("Cached package hash mismatch: %s" % case["name"])
			return
		manager.clear_pending_update()
		if manager.get_current_state() != 0 or FileAccess.file_exists(cached_path):
			_fail("Pending package cleanup failed: %s" % case["name"])
			return

	print("Local plugin package runtime tests passed")
