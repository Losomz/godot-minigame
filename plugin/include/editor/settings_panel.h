#pragma once

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/tab_bar.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace toolkit {
namespace editor {

class SettingsPanel : public godot::MarginContainer {
	GDCLASS(SettingsPanel, godot::MarginContainer);

private:
	godot::ScrollContainer *scroll_container = nullptr;
	godot::VBoxContainer *main_vbox = nullptr;

	godot::Label *current_template_label = nullptr;
	godot::TabBar *template_view_tabs = nullptr;
	godot::HBoxContainer *remote_catalog_controls = nullptr;
	godot::LineEdit *remote_catalog_input = nullptr;
	godot::Button *refresh_remote_catalog_button = nullptr;
	godot::OptionButton *template_selector = nullptr;
	godot::Button *set_current_template_button = nullptr;
	godot::Button *cache_template_button = nullptr;
	godot::Button *import_local_template_button = nullptr;
	godot::Label *template_status_label = nullptr;
	godot::ProgressBar *template_progress = nullptr;
	godot::EditorFileDialog *local_template_file_dialog = nullptr;

	godot::Label *plugin_version_label = nullptr;
	godot::Label *plugin_update_label = nullptr;
	godot::Button *check_plugin_update_button = nullptr;
	godot::Button *download_plugin_update_button = nullptr;
	godot::Button *select_local_plugin_button = nullptr;
	godot::EditorFileDialog *local_plugin_file_dialog = nullptr;
	godot::ConfirmationDialog *update_restart_dialog = nullptr;

	godot::CheckBox *proxy_enabled_check = nullptr;
	godot::LineEdit *proxy_host_input = nullptr;
	godot::SpinBox *proxy_port_input = nullptr;
	godot::Button *apply_proxy_button = nullptr;
	godot::Label *proxy_status_label = nullptr;

	void refresh_template_view();
	void refresh_template_choices();
	void refresh_template_action_state();
	void refresh_plugin_update_controls();
	void _on_template_view_changed(int index);
	void _on_refresh_remote_catalog_pressed();
	void _on_template_selected(int index);
	void _on_set_current_template_pressed();
	void _on_cache_template_pressed();
	void _on_import_local_template_pressed();
	void _on_local_template_file_selected(const godot::String &path);
	void _on_versions_loaded();
	void _on_versions_refresh_failed(int error_code);
	void _on_active_template_changed(const godot::Dictionary &template_info);
	void _on_template_download_progress(const godot::String &filename, float progress);
	void _on_template_download_finished(const godot::String &filename, bool success);

	void _on_check_plugin_update_pressed();
	void _on_download_plugin_update_pressed();
	void _on_select_local_plugin_pressed();
	void _on_local_plugin_file_selected(const godot::String &path);
	void _on_plugin_update_state_changed(int state);
	void _on_plugin_update_available(const godot::Dictionary &version_info);
	void _on_plugin_update_download_finished(bool success);
	void _on_confirm_update_restart();
	void _on_plugin_update_error(const godot::String &message);
	void refresh_proxy_settings();
	void _on_proxy_enabled_toggled(bool enabled);
	void _on_apply_proxy_pressed();
	godot::String get_installed_plugin_version() const;

protected:
	static void _bind_methods();

public:
	SettingsPanel();
	~SettingsPanel();
	virtual void _ready() override;
	void create_interface();
};

} // namespace editor
} // namespace toolkit
