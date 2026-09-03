#include "editor/settings_panel.h"

#include "core/network_proxy.h"
#include "core/types.h"
#include "core/update_manager.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/grid_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/text_server.hpp>

using namespace godot;

namespace toolkit {
namespace editor {

namespace {

void restore_file_dialog_path(EditorFileDialog *dialog, const String &path) {
	if (!dialog || path.is_empty()) {
		return;
	}
	const String normalized = path.simplify_path();
	if (FileAccess::file_exists(normalized)) {
		dialog->set_current_path(normalized);
		return;
	}
	const String directory = normalized.get_base_dir();
	if (DirAccess::dir_exists_absolute(directory)) {
		dialog->set_current_dir(directory);
	}
}

void configure_field_label(Label *label) {
	label->set_custom_minimum_size(Vector2(96, 0));
	label->set_vertical_alignment(VerticalAlignment::VERTICAL_ALIGNMENT_CENTER);
}

void configure_status_label(Label *label) {
	label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
}

} // namespace

SettingsPanel::SettingsPanel() {
	set_name("Settings");
	set_anchors_preset(Control::PRESET_FULL_RECT);
	add_theme_constant_override("margin_left", 12);
	add_theme_constant_override("margin_right", 12);
	add_theme_constant_override("margin_top", 8);
	add_theme_constant_override("margin_bottom", 8);
}

SettingsPanel::~SettingsPanel() {
}

void SettingsPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh_template_view"), &SettingsPanel::refresh_template_view);
	ClassDB::bind_method(D_METHOD("refresh_template_choices"), &SettingsPanel::refresh_template_choices);
	ClassDB::bind_method(D_METHOD("_on_template_view_changed", "index"), &SettingsPanel::_on_template_view_changed);
	ClassDB::bind_method(D_METHOD("_on_refresh_remote_catalog_pressed"), &SettingsPanel::_on_refresh_remote_catalog_pressed);
	ClassDB::bind_method(D_METHOD("_on_template_selected", "index"), &SettingsPanel::_on_template_selected);
	ClassDB::bind_method(D_METHOD("_on_set_current_template_pressed"), &SettingsPanel::_on_set_current_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_cache_template_pressed"), &SettingsPanel::_on_cache_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_import_local_template_pressed"), &SettingsPanel::_on_import_local_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_local_template_file_selected", "path"), &SettingsPanel::_on_local_template_file_selected);
	ClassDB::bind_method(D_METHOD("_on_versions_loaded"), &SettingsPanel::_on_versions_loaded);
	ClassDB::bind_method(D_METHOD("_on_versions_refresh_failed", "error_code"), &SettingsPanel::_on_versions_refresh_failed);
	ClassDB::bind_method(D_METHOD("_on_active_template_changed", "template_info"), &SettingsPanel::_on_active_template_changed);
	ClassDB::bind_method(D_METHOD("_on_template_download_progress", "filename", "progress"), &SettingsPanel::_on_template_download_progress);
	ClassDB::bind_method(D_METHOD("_on_template_download_finished", "filename", "success"), &SettingsPanel::_on_template_download_finished);

	ClassDB::bind_method(D_METHOD("_on_check_plugin_update_pressed"), &SettingsPanel::_on_check_plugin_update_pressed);
	ClassDB::bind_method(D_METHOD("_on_download_plugin_update_pressed"), &SettingsPanel::_on_download_plugin_update_pressed);
	ClassDB::bind_method(D_METHOD("_on_select_local_plugin_pressed"), &SettingsPanel::_on_select_local_plugin_pressed);
	ClassDB::bind_method(D_METHOD("_on_local_plugin_file_selected", "path"), &SettingsPanel::_on_local_plugin_file_selected);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_state_changed", "state"), &SettingsPanel::_on_plugin_update_state_changed);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_available", "version_info"), &SettingsPanel::_on_plugin_update_available);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_download_finished", "success"), &SettingsPanel::_on_plugin_update_download_finished);
	ClassDB::bind_method(D_METHOD("_on_confirm_update_restart"), &SettingsPanel::_on_confirm_update_restart);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_error", "message"), &SettingsPanel::_on_plugin_update_error);
	ClassDB::bind_method(D_METHOD("refresh_proxy_settings"), &SettingsPanel::refresh_proxy_settings);
	ClassDB::bind_method(D_METHOD("_on_proxy_enabled_toggled", "enabled"), &SettingsPanel::_on_proxy_enabled_toggled);
	ClassDB::bind_method(D_METHOD("_on_apply_proxy_pressed"), &SettingsPanel::_on_apply_proxy_pressed);
}

void SettingsPanel::_ready() {
	create_interface();

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
		manager->connect("versions_loaded", callable_mp(this, &SettingsPanel::_on_versions_loaded));
		manager->connect("versions_refresh_failed", callable_mp(this, &SettingsPanel::_on_versions_refresh_failed));
		manager->connect("template_download_progress", callable_mp(this, &SettingsPanel::_on_template_download_progress));
		manager->connect("template_download_finished", callable_mp(this, &SettingsPanel::_on_template_download_finished));
		manager->connect("active_template_changed", callable_mp(this, &SettingsPanel::_on_active_template_changed));
		manager->connect("template_inventory_changed", callable_mp(this, &SettingsPanel::refresh_template_choices));
		remote_catalog_input->set_text(String(manager->call("get_remote_catalog_url")));
	}

	UpdateManager *update_manager = UpdateManager::get_singleton();
	if (update_manager) {
		update_manager->initialize();
		update_manager->connect("update_state_changed", callable_mp(this, &SettingsPanel::_on_plugin_update_state_changed));
		update_manager->connect("update_available", callable_mp(this, &SettingsPanel::_on_plugin_update_available));
		update_manager->connect("download_finished", callable_mp(this, &SettingsPanel::_on_plugin_update_download_finished));
		update_manager->connect("error", callable_mp(this, &SettingsPanel::_on_plugin_update_error));
		const String install_message = update_manager->get_last_install_message();
		if (!install_message.is_empty()) {
			plugin_update_label->set_text(install_message);
		}
	}

	refresh_template_view();
	refresh_plugin_update_controls();
	refresh_proxy_settings();
}

void SettingsPanel::create_interface() {
	VBoxContainer *layout_root = memnew(VBoxContainer);
	layout_root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	layout_root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(layout_root);

	scroll_container = memnew(ScrollContainer);
	scroll_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_container->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll_container->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	scroll_container->set_follow_focus(true);
	layout_root->add_child(scroll_container);

	main_vbox = memnew(VBoxContainer);
	main_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_theme_constant_override("separation", 8);
	scroll_container->add_child(main_vbox);

	auto add_section_title = [this](const String &text) {
		Label *title = memnew(Label);
		title->set_text(text);
		title->set_theme_type_variation("HeaderSmall");
		title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		main_vbox->add_child(title);
	};

	add_section_title(String::utf8("模板"));

	current_template_label = memnew(Label);
	current_template_label->set_text(String::utf8("当前模板：未选择"));
	configure_status_label(current_template_label);
	main_vbox->add_child(current_template_label);

	template_view_tabs = memnew(TabBar);
	template_view_tabs->add_tab(String::utf8("远端"));
	template_view_tabs->add_tab(String::utf8("本地"));
	template_view_tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	template_view_tabs->connect("tab_changed", callable_mp(this, &SettingsPanel::_on_template_view_changed));
	main_vbox->add_child(template_view_tabs);

	remote_catalog_controls = memnew(HBoxContainer);
	remote_catalog_controls->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(remote_catalog_controls);

	Label *catalog_label = memnew(Label);
	catalog_label->set_text(String::utf8("模板源"));
	configure_field_label(catalog_label);
	remote_catalog_controls->add_child(catalog_label);

	remote_catalog_input = memnew(LineEdit);
	remote_catalog_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	remote_catalog_input->set_placeholder("https://.../templates.json");
	remote_catalog_controls->add_child(remote_catalog_input);

	refresh_remote_catalog_button = memnew(Button);
	refresh_remote_catalog_button->set_text(String::utf8("刷新"));
	refresh_remote_catalog_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_refresh_remote_catalog_pressed));
	remote_catalog_controls->add_child(refresh_remote_catalog_button);

	GridContainer *template_form = memnew(GridContainer);
	template_form->set_columns(2);
	template_form->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(template_form);

	Label *template_label = memnew(Label);
	template_label->set_text(String::utf8("模板"));
	configure_field_label(template_label);
	template_form->add_child(template_label);

	template_selector = memnew(OptionButton);
	template_selector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	template_selector->connect("item_selected", callable_mp(this, &SettingsPanel::_on_template_selected));
	template_form->add_child(template_selector);

	HBoxContainer *template_actions = memnew(HBoxContainer);
	template_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(template_actions);

	set_current_template_button = memnew(Button);
	set_current_template_button->set_text(String::utf8("设为当前"));
	set_current_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_set_current_template_pressed));
	template_actions->add_child(set_current_template_button);

	cache_template_button = memnew(Button);
	cache_template_button->set_text(String::utf8("缓存到本地"));
	cache_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_cache_template_pressed));
	template_actions->add_child(cache_template_button);

	import_local_template_button = memnew(Button);
	import_local_template_button->set_text(String::utf8("导入本地 TPZ..."));
	import_local_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_import_local_template_pressed));
	template_actions->add_child(import_local_template_button);

	template_status_label = memnew(Label);
	template_status_label->set_text(String::utf8("模板列表已就绪"));
	configure_status_label(template_status_label);
	main_vbox->add_child(template_status_label);

	template_progress = memnew(ProgressBar);
	template_progress->set_custom_minimum_size(Vector2(0, 6));
	template_progress->set_min(0.0);
	template_progress->set_max(1.0);
	template_progress->set_show_percentage(false);
	template_progress->set_visible(false);
	main_vbox->add_child(template_progress);

	HSeparator *separator = memnew(HSeparator);
	separator->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(separator);

	add_section_title(String::utf8("插件更新"));

	plugin_version_label = memnew(Label);
	plugin_version_label->set_text(String::utf8("当前版本：") + get_installed_plugin_version());
	configure_status_label(plugin_version_label);
	main_vbox->add_child(plugin_version_label);

	plugin_update_label = memnew(Label);
	plugin_update_label->set_text(String::utf8("尚未检查更新"));
	configure_status_label(plugin_update_label);
	main_vbox->add_child(plugin_update_label);

	HBoxContainer *plugin_actions = memnew(HBoxContainer);
	plugin_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(plugin_actions);

	check_plugin_update_button = memnew(Button);
	check_plugin_update_button->set_text(String::utf8("检查更新"));
	check_plugin_update_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_check_plugin_update_pressed));
	plugin_actions->add_child(check_plugin_update_button);

	download_plugin_update_button = memnew(Button);
	download_plugin_update_button->set_text(String::utf8("下载更新"));
	download_plugin_update_button->set_disabled(true);
	download_plugin_update_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_download_plugin_update_pressed));
	plugin_actions->add_child(download_plugin_update_button);

	select_local_plugin_button = memnew(Button);
	select_local_plugin_button->set_text(String::utf8("从本地安装 ZIP..."));
	select_local_plugin_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_select_local_plugin_pressed));
	plugin_actions->add_child(select_local_plugin_button);

	HSeparator *network_separator = memnew(HSeparator);
	network_separator->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(network_separator);

	add_section_title(String::utf8("网络代理"));

	HBoxContainer *proxy_controls = memnew(HBoxContainer);
	proxy_controls->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(proxy_controls);

	proxy_enabled_check = memnew(CheckBox);
	proxy_enabled_check->set_text(String::utf8("使用代理"));
	proxy_enabled_check->connect("toggled", callable_mp(this, &SettingsPanel::_on_proxy_enabled_toggled));
	proxy_controls->add_child(proxy_enabled_check);

	Label *proxy_host_label = memnew(Label);
	proxy_host_label->set_text(String::utf8("主机"));
	proxy_controls->add_child(proxy_host_label);

	proxy_host_input = memnew(LineEdit);
	proxy_host_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	proxy_host_input->set_placeholder("127.0.0.1");
	proxy_controls->add_child(proxy_host_input);

	Label *proxy_port_label = memnew(Label);
	proxy_port_label->set_text(String::utf8("端口"));
	proxy_controls->add_child(proxy_port_label);

	proxy_port_input = memnew(SpinBox);
	proxy_port_input->set_custom_minimum_size(Vector2(120, 0));
	proxy_port_input->set_min(1);
	proxy_port_input->set_max(65535);
	proxy_port_input->set_step(1);
	proxy_port_input->set_allow_greater(false);
	proxy_port_input->set_allow_lesser(false);
	proxy_controls->add_child(proxy_port_input);

	apply_proxy_button = memnew(Button);
	apply_proxy_button->set_text(String::utf8("应用"));
	apply_proxy_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_apply_proxy_pressed));
	proxy_controls->add_child(apply_proxy_button);

	proxy_status_label = memnew(Label);
	configure_status_label(proxy_status_label);
	main_vbox->add_child(proxy_status_label);

	local_template_file_dialog = memnew(EditorFileDialog);
	local_template_file_dialog->set_title(String::utf8("导入本地微信模板"));
	local_template_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	local_template_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	local_template_file_dialog->add_filter("*.tpz", String::utf8("微信小游戏模板"));
	local_template_file_dialog->connect("file_selected", callable_mp(this, &SettingsPanel::_on_local_template_file_selected));
	add_child(local_template_file_dialog);

	local_plugin_file_dialog = memnew(EditorFileDialog);
	local_plugin_file_dialog->set_title(String::utf8("选择本地插件包"));
	local_plugin_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	local_plugin_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	local_plugin_file_dialog->add_filter("*.zip", String::utf8("Godot Minigame 插件包"));
	local_plugin_file_dialog->connect("file_selected", callable_mp(this, &SettingsPanel::_on_local_plugin_file_selected));
	add_child(local_plugin_file_dialog);

	update_restart_dialog = memnew(ConfirmationDialog);
	update_restart_dialog->set_title(String::utf8("安装插件更新"));
	update_restart_dialog->get_ok_button()->set_text(String::utf8("安装并重启"));
	update_restart_dialog->connect("confirmed", callable_mp(this, &SettingsPanel::_on_confirm_update_restart));
	add_child(update_restart_dialog);
}

void SettingsPanel::refresh_template_view() {
	String view = "remote";
	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
		view = String(manager->call("get_template_view"));
	}
	const bool local = view == "local";
	if (template_view_tabs) {
		template_view_tabs->set_current_tab(local ? 1 : 0);
	}
	if (remote_catalog_controls) {
		remote_catalog_controls->set_visible(!local);
	}
	if (cache_template_button) {
		cache_template_button->set_visible(!local);
	}
	if (import_local_template_button) {
		import_local_template_button->set_visible(local);
	}
	refresh_template_choices();
}

void SettingsPanel::refresh_template_choices() {
	if (!template_selector || !Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const String view = String(manager->call("get_template_view"));
	const Array choices = manager->call(view == "local" ? "get_local_template_choices" : "get_remote_template_choices");
	const Dictionary active = manager->call("get_active_template_info");
	const String active_id = String(active.get("id", ""));
	String previous_id;
	if (template_selector->get_selected() >= 0) {
		const Dictionary previous = template_selector->get_item_metadata(template_selector->get_selected());
		previous_id = String(previous.get("id", ""));
	}

	template_selector->clear();
	int selected_index = -1;
	for (int i = 0; i < choices.size(); i++) {
		const Dictionary choice = choices[i];
		String text = String(choice.get("display_name", choice.get("filename", "")));
		if (view == "remote") {
			const String tag = String(choice.get("release_tag", ""));
			if (!tag.is_empty()) {
				text += " [" + tag + "]";
			}
			if (String(choice.get("status", "stable")) == "prerelease") {
				text += String::utf8(" · 预发布");
			}
			text += bool(choice.get("available", false)) ? String::utf8(" · 已缓存") : String::utf8(" · 未缓存");
		} else {
			text += String::utf8(" · ") + String(choice.get("origin", String::utf8("本地")));
		}
		template_selector->add_item(text);
		template_selector->set_item_metadata(i, choice);
		const String id = String(choice.get("id", ""));
		if (id == previous_id || (selected_index < 0 && id == active_id)) {
			selected_index = i;
		}
	}
	if (selected_index < 0 && !choices.is_empty()) {
		selected_index = 0;
	}
	if (selected_index >= 0) {
		template_selector->select(selected_index);
	}

	const String display_name = String(active.get("display_name", String::utf8("未选择")));
	const String origin = String(active.get("origin", ""));
	current_template_label->set_text(String::utf8("当前模板：") + display_name +
			(origin.is_empty() ? String() : String::utf8(" · ") + origin));
	refresh_template_action_state();
}

void SettingsPanel::refresh_template_action_state() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const bool busy = bool(manager->call("is_prefetch_active"));
	const bool has_selection = template_selector && template_selector->get_selected() >= 0;
	Dictionary choice;
	if (has_selection) {
		choice = template_selector->get_item_metadata(template_selector->get_selected());
	}
	const Dictionary active = manager->call("get_active_template_info");
	const bool already_current = has_selection && String(choice.get("id", "")) == String(active.get("id", ""));
	const bool local = String(manager->call("get_template_view")) == "local";

	template_selector->set_disabled(busy || !has_selection);
	template_view_tabs->set_tab_disabled(0, busy);
	template_view_tabs->set_tab_disabled(1, busy);
	remote_catalog_input->set_editable(!busy);
	refresh_remote_catalog_button->set_disabled(busy);
	set_current_template_button->set_disabled(busy || !has_selection || already_current);
	cache_template_button->set_disabled(busy || !has_selection || bool(choice.get("available", false)));
	import_local_template_button->set_disabled(busy);
	cache_template_button->set_visible(!local);
	import_local_template_button->set_visible(local);
	template_progress->set_visible(busy);
}

void SettingsPanel::_on_template_view_changed(int index) {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	manager->call("set_template_view", index == 1 ? String("local") : String("remote"));
	refresh_template_view();
}

void SettingsPanel::_on_refresh_remote_catalog_pressed() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const int result = int(manager->call("refresh_remote_catalog", remote_catalog_input->get_text()));
	if (result == OK) {
		template_status_label->set_text(String::utf8("正在刷新远端模板列表..."));
	} else if (result == ERR_BUSY) {
		template_status_label->set_text(String::utf8("当前有模板操作正在进行"));
	} else {
		template_status_label->set_text(String::utf8("模板源必须是有效的 HTTP(S) JSON 地址"));
	}
	refresh_template_action_state();
}

void SettingsPanel::_on_template_selected(int) {
	refresh_template_action_state();
}

void SettingsPanel::_on_set_current_template_pressed() {
	if (!template_selector || template_selector->get_selected() < 0 ||
			!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const Dictionary choice = template_selector->get_item_metadata(template_selector->get_selected());
	const int result = int(manager->call("set_current_template", String(choice.get("id", ""))));
	if (result == OK) {
		template_status_label->set_text(bool(choice.get("available", false))
				? String::utf8("当前模板已切换")
				: String::utf8("正在缓存模板，完成后设为当前"));
	} else if (result == ERR_BUSY) {
		template_status_label->set_text(String::utf8("当前有模板操作正在进行"));
	} else {
		template_status_label->set_text(String::utf8("无法使用所选模板"));
	}
	refresh_template_action_state();
}

void SettingsPanel::_on_cache_template_pressed() {
	if (!template_selector || template_selector->get_selected() < 0 ||
			!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const Dictionary choice = template_selector->get_item_metadata(template_selector->get_selected());
	const int result = int(manager->call("cache_template", String(choice.get("id", ""))));
	if (result == OK) {
		template_status_label->set_text(String::utf8("正在缓存模板..."));
	} else if (result == ERR_BUSY) {
		template_status_label->set_text(String::utf8("当前有模板操作正在进行"));
	} else {
		template_status_label->set_text(String::utf8("模板缓存失败"));
	}
	refresh_template_action_state();
}

void SettingsPanel::_on_import_local_template_pressed() {
	if (!local_template_file_dialog) {
		return;
	}
	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
		restore_file_dialog_path(local_template_file_dialog, String(manager->call("get_last_local_template_path")));
	}
	local_template_file_dialog->popup_file_dialog();
}

void SettingsPanel::_on_local_template_file_selected(const String &path) {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const int result = int(manager->call("import_local_template", path));
	if (result == OK) {
		manager->call("set_template_view", "local");
		template_status_label->set_text(String::utf8("模板已导入本地列表，请选择后设为当前"));
		refresh_template_view();
	} else {
		template_status_label->set_text(String::utf8("所选文件不是有效的 TPZ 模板"));
	}
}

void SettingsPanel::_on_versions_loaded() {
	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
		remote_catalog_input->set_text(String(manager->call("get_remote_catalog_url")));
	}
	template_status_label->set_text(String::utf8("远端模板列表已更新"));
	refresh_template_choices();
}

void SettingsPanel::_on_versions_refresh_failed(int) {
	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *manager = Engine::get_singleton()->get_singleton("TemplateManager");
		remote_catalog_input->set_text(String(manager->call("get_remote_catalog_url")));
	}
	template_status_label->set_text(String::utf8("远端模板源获取失败，已保留原列表"));
	refresh_template_choices();
}

void SettingsPanel::_on_active_template_changed(const Dictionary &) {
	refresh_template_choices();
}

void SettingsPanel::_on_template_download_progress(const String &filename, float progress) {
	template_progress->set_visible(true);
	template_progress->set_value(progress);
	template_status_label->set_text(String::utf8("正在下载：") + filename);
}

void SettingsPanel::_on_template_download_finished(const String &, bool success) {
	template_progress->set_visible(false);
	template_progress->set_value(0.0);
	template_status_label->set_text(success ? String::utf8("模板已缓存到本地") : String::utf8("模板下载或校验失败，当前模板未改变"));
	refresh_template_choices();
}

void SettingsPanel::_on_check_plugin_update_pressed() {
	UpdateManager *manager = UpdateManager::get_singleton();
	if (!manager) {
		_on_plugin_update_error("Update manager is unavailable.");
		return;
	}
	manager->clear_pending_update();
	manager->set_update_channel("remote");
	plugin_update_label->set_text(String::utf8("正在检查更新..."));
	manager->check_for_updates(get_installed_plugin_version());
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_download_plugin_update_pressed() {
	UpdateManager *manager = UpdateManager::get_singleton();
	if (!manager) {
		_on_plugin_update_error("Update manager is unavailable.");
		return;
	}
	if (manager->get_current_state() == UpdateManager::STATE_DOWNLOADED) {
		update_restart_dialog->set_title(String::utf8("安装插件更新"));
		update_restart_dialog->set_text(String::utf8("将关闭 Godot、替换插件并重新打开当前项目。是否继续？"));
		update_restart_dialog->popup_centered();
		return;
	}
	manager->download_update();
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_select_local_plugin_pressed() {
	UpdateManager *manager = UpdateManager::get_singleton();
	if (manager) {
		manager->clear_pending_update();
		restore_file_dialog_path(local_plugin_file_dialog, manager->get_last_local_package_path());
	}
	local_plugin_file_dialog->popup_file_dialog();
}

void SettingsPanel::_on_local_plugin_file_selected(const String &path) {
	UpdateManager *manager = UpdateManager::get_singleton();
	if (!manager) {
		_on_plugin_update_error("Update manager is unavailable.");
		return;
	}
	const Dictionary result = manager->select_local_package(path, get_installed_plugin_version());
	if (!bool(result.get("success", false))) {
		_on_plugin_update_error(String(result.get("error", "Invalid local plugin package.")));
		return;
	}
	const String current_version = get_installed_plugin_version();
	const String target_version = String(result.get("version", ""));
	const VersionInfo current = VersionInfo::from_string(current_version);
	const VersionInfo target = VersionInfo::from_string(target_version);
	String operation = String::utf8("同版本覆盖");
	if (target.is_newer_than(current)) {
		operation = String::utf8("升级");
	} else if (current.is_newer_than(target)) {
		operation = String::utf8("降级");
	}
	plugin_update_label->set_text(String::utf8("本地插件包已校验，版本 ") + target_version);
	update_restart_dialog->set_title(String::utf8("安装本地插件包"));
	update_restart_dialog->set_text(
			String::utf8("当前版本：") + current_version + "\n" +
			String::utf8("目标版本：") + target_version + "\n" +
			String::utf8("操作类型：") + operation + "\n\n" +
			String::utf8("将关闭 Godot、替换插件并重新打开当前项目。是否继续？"));
	update_restart_dialog->popup_centered();
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_plugin_update_state_changed(int state) {
	if (state == UpdateManager::STATE_UP_TO_DATE) {
		plugin_update_label->set_text(String::utf8("当前已是最新版本"));
	} else if (state == UpdateManager::STATE_CHECKING) {
		plugin_update_label->set_text(String::utf8("正在检查更新..."));
	} else if (state == UpdateManager::STATE_DOWNLOADING) {
		plugin_update_label->set_text(String::utf8("正在下载插件更新..."));
	} else if (state == UpdateManager::STATE_DOWNLOADED) {
		UpdateManager *manager = UpdateManager::get_singleton();
		const bool local = manager && String(manager->get_remote_version_info().get("channel", "")) == "local";
		plugin_update_label->set_text(local ? String::utf8("本地插件包已准备安装") : String::utf8("插件更新已下载"));
	} else if (state == UpdateManager::STATE_INSTALLING) {
		plugin_update_label->set_text(String::utf8("正在启动安装程序并重启 Godot..."));
	}
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_plugin_update_available(const Dictionary &version_info) {
	plugin_update_label->set_text(String::utf8("发现新版本 ") + String(version_info.get("version", "")));
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_plugin_update_download_finished(bool success) {
	plugin_update_label->set_text(success ? String::utf8("插件更新已下载") : String::utf8("插件更新下载失败"));
	refresh_plugin_update_controls();
}

void SettingsPanel::_on_confirm_update_restart() {
	UpdateManager *manager = UpdateManager::get_singleton();
	if (!manager) {
		_on_plugin_update_error("Update manager is unavailable.");
		return;
	}
	plugin_update_label->set_text(String::utf8("正在准备安装并重启 Godot..."));
	check_plugin_update_button->set_disabled(true);
	download_plugin_update_button->set_disabled(true);
	select_local_plugin_button->set_disabled(true);
	manager->restart_editor_for_update();
}

void SettingsPanel::_on_plugin_update_error(const String &message) {
	plugin_update_label->set_text(String::utf8("插件更新失败：") + message);
	refresh_plugin_update_controls();
}

void SettingsPanel::refresh_plugin_update_controls() {
	UpdateManager *manager = UpdateManager::get_singleton();
	const int state = manager ? int(manager->get_current_state()) : int(UpdateManager::STATE_ERROR);
	const bool busy = state == UpdateManager::STATE_CHECKING || state == UpdateManager::STATE_DOWNLOADING || state == UpdateManager::STATE_INSTALLING;
	check_plugin_update_button->set_disabled(busy || state == UpdateManager::STATE_DOWNLOADED);
	download_plugin_update_button->set_text(state == UpdateManager::STATE_DOWNLOADED ? String::utf8("安装并重启") : String::utf8("下载更新"));
	download_plugin_update_button->set_disabled(state != UpdateManager::STATE_UPDATE_AVAILABLE && state != UpdateManager::STATE_DOWNLOADED);
	select_local_plugin_button->set_disabled(busy);
	plugin_version_label->set_text(String::utf8("当前版本：") + get_installed_plugin_version());
}

void SettingsPanel::refresh_proxy_settings() {
	const Dictionary config = NetworkProxy::get_config();
	const bool enabled = bool(config.get("proxy_enabled", false));
	const String host = String(config.get("proxy_host", "127.0.0.1"));
	const int port = int(config.get("proxy_port", 7890));
	proxy_enabled_check->set_pressed_no_signal(enabled);
	proxy_host_input->set_text(host);
	proxy_port_input->set_value(port);
	_on_proxy_enabled_toggled(enabled);
	proxy_status_label->set_text(enabled
			? String::utf8("代理已启用：") + host + ":" + String::num_int64(port)
			: String::utf8("当前使用直连"));
}

void SettingsPanel::_on_proxy_enabled_toggled(bool enabled) {
	proxy_host_input->set_editable(enabled);
	proxy_port_input->set_editable(enabled);
}

void SettingsPanel::_on_apply_proxy_pressed() {
	const bool enabled = proxy_enabled_check->is_pressed();
	const String host = proxy_host_input->get_text().strip_edges();
	const int port = int(proxy_port_input->get_value());
	const Error result = NetworkProxy::save_config(enabled, host, port);
	if (result != OK) {
		proxy_status_label->set_text(String::utf8("代理设置无效，请检查主机和端口"));
		return;
	}
	refresh_proxy_settings();
}

String SettingsPanel::get_installed_plugin_version() const {
	Ref<ConfigFile> config;
	config.instantiate();
	String version = "0.0.0";
	if (config->load("res://addons/godot-minigame/plugin.cfg") == OK) {
		version = String(config->get_value("plugin", "version", version)).strip_edges();
	}
	return version;
}

} // namespace editor
} // namespace toolkit
