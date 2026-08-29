#pragma once

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace toolkit {

class PluginStateStore {
public:
	static godot::String get_root_dir();
	static godot::String get_state_path();
	static bool has_section(const godot::String &p_section);
	static godot::Dictionary load_section(const godot::String &p_section);
	static godot::Error save_section(const godot::String &p_section, const godot::Dictionary &p_value);

private:
	static godot::Dictionary load_state();
	static godot::Error save_state(const godot::Dictionary &p_state);
};

} // namespace toolkit
