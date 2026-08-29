@tool
extends EditorPlugin

var dock
var wechat_platform
var wechat_platform_added := false

func _enter_tree():
	_initialize_plugin()

func _exit_tree():
	_unregister_wechat_platform()

	# The editor is responsible for cleaning up the dock control.
	# Manually removing it in _exit_tree can cause errors during the editor's
	# shutdown sequence, as the dock manager might already be dismantled.
	if is_instance_valid(dock):
		dock.queue_free()

func _initialize_plugin():
	dock = _instantiate_dock()
	if dock:
		add_control_to_bottom_panel(dock, "Minigame")

	_register_wechat_platform()

func _instantiate_dock():
	const DOCK_CLASS_NAME := "GodotMinigameDock"
	if ClassDB.class_exists(DOCK_CLASS_NAME):
		var instance = ClassDB.instantiate(DOCK_CLASS_NAME)
		if instance is Control:
			return instance

		push_warning("Godot Minigame: %s is not a Control" % DOCK_CLASS_NAME)
		return null

	push_warning("Godot Minigame: %s class is not available" % DOCK_CLASS_NAME)
	printerr("[GodotMinigame][plugin.gd] dock class not found: ", DOCK_CLASS_NAME)
	return null

func _register_wechat_platform():
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
		printerr("[GodotMinigame][plugin.gd] failed to instantiate: ", PLATFORM_CLASS_NAME)
		return null

	push_warning("Godot Minigame: %s class is not available" % PLATFORM_CLASS_NAME)
	printerr("[GodotMinigame][plugin.gd] ClassDB missing ", PLATFORM_CLASS_NAME)
	return null

func _unregister_wechat_platform():
	if wechat_platform_added and wechat_platform and is_instance_valid(wechat_platform):
		remove_export_platform(wechat_platform)

	wechat_platform_added = false
	wechat_platform = null
