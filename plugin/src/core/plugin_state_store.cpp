#include "core/plugin_state_store.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_paths.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>

using namespace godot;

namespace toolkit {

namespace {

constexpr int STATE_SCHEMA_VERSION = 1;

Dictionary make_empty_state() {
	Dictionary state;
	state["schema_version"] = STATE_SCHEMA_VERSION;
	return state;
}

} // namespace

String PluginStateStore::get_root_dir() {
	EditorInterface *editor = EditorInterface::get_singleton();
	String root;
	if (editor && editor->get_editor_paths()) {
		root = editor->get_editor_paths()->get_cache_dir().path_join("godot-minigame");
	} else {
		root = OS::get_singleton()->get_cache_dir().path_join("godot-minigame");
	}
	DirAccess::make_dir_recursive_absolute(root);
	return root;
}

String PluginStateStore::get_state_path() {
	return get_root_dir().path_join("state.json");
}

Dictionary PluginStateStore::load_state() {
	const String path = get_state_path();
	if (!FileAccess::file_exists(path)) {
		return make_empty_state();
	}

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		return make_empty_state();
	}
	const Variant parsed = JSON::parse_string(file->get_as_text());
	file->close();
	if (parsed.get_type() != Variant::DICTIONARY) {
		return make_empty_state();
	}

	Dictionary state = parsed;
	if (int(state.get("schema_version", 0)) != STATE_SCHEMA_VERSION) {
		return make_empty_state();
	}
	return state;
}

bool PluginStateStore::has_section(const String &p_section) {
	return load_state().has(p_section);
}

Dictionary PluginStateStore::load_section(const String &p_section) {
	const Variant value = load_state().get(p_section, Dictionary());
	return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

Error PluginStateStore::save_section(const String &p_section, const Dictionary &p_value) {
	Dictionary state = load_state();
	state["schema_version"] = STATE_SCHEMA_VERSION;
	state[p_section] = p_value.duplicate(true);
	return save_state(state);
}

Error PluginStateStore::save_state(const Dictionary &p_state) {
	const String target_path = get_state_path();
	const String temporary_path = target_path + String(".tmp.") + String::num_int64(OS::get_singleton()->get_process_id());
	const String backup_path = target_path + String(".backup");
	DirAccess::remove_absolute(temporary_path);
	DirAccess::remove_absolute(backup_path);

	Ref<FileAccess> file = FileAccess::open(temporary_path, FileAccess::WRITE);
	if (file.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	file->store_string(JSON::stringify(p_state, "  ") + "\n");
	file->flush();
	file->close();

	const bool had_previous = FileAccess::file_exists(target_path);
	if (had_previous) {
		const Error backup_error = DirAccess::rename_absolute(target_path, backup_path);
		if (backup_error != OK) {
			DirAccess::remove_absolute(temporary_path);
			return backup_error;
		}
	}

	const Error publish_error = DirAccess::rename_absolute(temporary_path, target_path);
	if (publish_error != OK) {
		if (had_previous) {
			DirAccess::rename_absolute(backup_path, target_path);
		}
		DirAccess::remove_absolute(temporary_path);
		return publish_error;
	}
	if (had_previous) {
		DirAccess::remove_absolute(backup_path);
	}
	return OK;
}

} // namespace toolkit
