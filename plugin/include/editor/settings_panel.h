#pragma once

#include <godot_cpp/classes/margin_container.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/h_flow_container.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/editor_file_dialog.hpp>
#include <godot_cpp/core/class_db.hpp>

namespace toolkit {
namespace editor {

class SettingsPanel : public godot::MarginContainer {
	GDCLASS(SettingsPanel, godot::MarginContainer);

private:
	godot::ScrollContainer *scroll_container = nullptr;
	godot::VBoxContainer *main_vbox = nullptr;
	godot::Label *plugin_update_label = nullptr;
	godot::OptionButton *plugin_update_channel_selector = nullptr;
	godot::VBoxContainer *remote_update_controls = nullptr;
	godot::VBoxContainer *local_update_controls = nullptr;
	godot::Button *check_plugin_update_button = nullptr;
	godot::Button *download_plugin_update_button = nullptr;
	godot::Button *select_local_plugin_button = nullptr;
	godot::Button *install_local_plugin_button = nullptr;
	godot::Label *local_plugin_package_label = nullptr;
	godot::EditorFileDialog *local_plugin_file_dialog = nullptr;
	godot::Label *distribution_provider_title_label = nullptr;
	godot::OptionButton *distribution_provider_selector = nullptr;
	godot::Label *owner_label = nullptr;
	godot::LineEdit *owner_input = nullptr;
	godot::Label *repo_label = nullptr;
	godot::LineEdit *repo_input = nullptr;
	godot::Label *tag_label = nullptr;
	godot::LineEdit *tag_input = nullptr;
	godot::Button *save_config_button = nullptr;
	godot::Button *reset_config_button = nullptr;
	godot::Button *refresh_versions_button = nullptr;
	godot::Label *action_status_label = nullptr;
	godot::Label *template_cache_label = nullptr;
	godot::OptionButton *template_version_selector = nullptr;
	godot::LineEdit *custom_template_url_input = nullptr;
	godot::Button *use_custom_template_button = nullptr;
	godot::Button *select_local_template_button = nullptr;
	godot::EditorFileDialog *local_template_file_dialog = nullptr;
	godot::Button *prefetch_template_button = nullptr;
	godot::Button *replace_template_button = nullptr;
	godot::Button *remove_template_button = nullptr;
	godot::Button *clear_templates_button = nullptr;
	godot::ProgressBar *template_cache_progress = nullptr;
	godot::ConfirmationDialog *update_restart_dialog = nullptr;
	godot::ConfirmationDialog *clear_templates_dialog = nullptr;

	void refresh_distribution_info();
	void refresh_template_cache_info();
	void _on_check_plugin_update_pressed();
	void _on_download_plugin_update_pressed();
	void _on_plugin_update_channel_selected(int index);
	void _on_select_local_plugin_pressed();
	void _on_local_plugin_file_selected(const godot::String &path);
	void _on_install_local_plugin_pressed();
	void _on_plugin_update_state_changed(int state);
	void _on_plugin_update_available(const godot::Dictionary &version_info);
	void _on_plugin_update_download_finished(bool success);
	void _on_confirm_update_restart();
	void _on_plugin_update_error(const godot::String &message);
	void _on_distribution_provider_selected(int index);
	void _on_save_distribution_config_pressed();
	void _on_reset_config_pressed();
	void _on_refresh_versions_pressed();
	void _on_versions_loaded();
	void _on_versions_refresh_failed(int error_code);
	void _on_active_template_changed(const godot::Dictionary &template_info);
	void _on_template_version_selected(int index);
	void _on_use_custom_template_pressed();
	void _on_select_local_template_pressed();
	void _on_local_template_file_selected(const godot::String &path);
	void _on_prefetch_template_pressed();
	void _on_replace_template_pressed();
	void _on_remove_template_pressed();
	void _on_clear_templates_pressed();
	void _on_confirm_clear_templates();
	void _on_template_download_progress(const godot::String &filename, float progress);
	void _on_template_cache_download_finished(const godot::String &filename, bool success);
	void load_plugin_update_channel();
	void save_plugin_update_channel() const;
	void refresh_plugin_update_channel();
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
