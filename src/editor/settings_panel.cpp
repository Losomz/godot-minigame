#include "editor/settings_panel.h"
#include "core/update_manager.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

using namespace godot;

namespace toolkit {
namespace editor {

SettingsPanel::SettingsPanel() {
	set_name("Settings");

	set_anchors_preset(Control::PRESET_FULL_RECT);
	add_theme_constant_override("margin_left", 4);
	add_theme_constant_override("margin_right", 4);
	add_theme_constant_override("margin_top", 4);
	add_theme_constant_override("margin_bottom", 4);
}

SettingsPanel::~SettingsPanel() {
}

void SettingsPanel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh_distribution_info"), &SettingsPanel::refresh_distribution_info);
	ClassDB::bind_method(D_METHOD("refresh_template_cache_info"), &SettingsPanel::refresh_template_cache_info);
	ClassDB::bind_method(D_METHOD("_on_check_plugin_update_pressed"), &SettingsPanel::_on_check_plugin_update_pressed);
	ClassDB::bind_method(D_METHOD("_on_download_plugin_update_pressed"), &SettingsPanel::_on_download_plugin_update_pressed);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_state_changed", "state"), &SettingsPanel::_on_plugin_update_state_changed);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_available", "version_info"), &SettingsPanel::_on_plugin_update_available);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_download_finished", "success"), &SettingsPanel::_on_plugin_update_download_finished);
	ClassDB::bind_method(D_METHOD("_on_confirm_update_restart"), &SettingsPanel::_on_confirm_update_restart);
	ClassDB::bind_method(D_METHOD("_on_plugin_update_error", "message"), &SettingsPanel::_on_plugin_update_error);
	ClassDB::bind_method(D_METHOD("_on_distribution_provider_selected", "index"), &SettingsPanel::_on_distribution_provider_selected);
	ClassDB::bind_method(D_METHOD("_on_save_distribution_config_pressed"), &SettingsPanel::_on_save_distribution_config_pressed);
	ClassDB::bind_method(D_METHOD("_on_reset_config_pressed"), &SettingsPanel::_on_reset_config_pressed);
	ClassDB::bind_method(D_METHOD("_on_refresh_versions_pressed"), &SettingsPanel::_on_refresh_versions_pressed);
	ClassDB::bind_method(D_METHOD("_on_versions_loaded"), &SettingsPanel::_on_versions_loaded);
	ClassDB::bind_method(D_METHOD("_on_versions_refresh_failed", "error_code"), &SettingsPanel::_on_versions_refresh_failed);
	ClassDB::bind_method(D_METHOD("_on_active_template_changed", "template_info"), &SettingsPanel::_on_active_template_changed);
	ClassDB::bind_method(D_METHOD("_on_template_version_selected", "index"), &SettingsPanel::_on_template_version_selected);
	ClassDB::bind_method(D_METHOD("_on_use_custom_template_pressed"), &SettingsPanel::_on_use_custom_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_prefetch_template_pressed"), &SettingsPanel::_on_prefetch_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_replace_template_pressed"), &SettingsPanel::_on_replace_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_remove_template_pressed"), &SettingsPanel::_on_remove_template_pressed);
	ClassDB::bind_method(D_METHOD("_on_clear_templates_pressed"), &SettingsPanel::_on_clear_templates_pressed);
	ClassDB::bind_method(D_METHOD("_on_confirm_clear_templates"), &SettingsPanel::_on_confirm_clear_templates);
	ClassDB::bind_method(D_METHOD("_on_template_download_progress", "filename", "progress"), &SettingsPanel::_on_template_download_progress);
	ClassDB::bind_method(D_METHOD("_on_template_cache_download_finished", "filename", "success"), &SettingsPanel::_on_template_cache_download_finished);
}

void SettingsPanel::_ready() {
	create_interface();

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && !template_manager->is_connected("versions_loaded", callable_mp(this, &SettingsPanel::_on_versions_loaded))) {
			template_manager->connect("versions_loaded", callable_mp(this, &SettingsPanel::_on_versions_loaded));
		}
		if (template_manager && !template_manager->is_connected("versions_refresh_failed", callable_mp(this, &SettingsPanel::_on_versions_refresh_failed))) {
			template_manager->connect("versions_refresh_failed", callable_mp(this, &SettingsPanel::_on_versions_refresh_failed));
		}
		if (template_manager && !template_manager->is_connected("template_download_progress", callable_mp(this, &SettingsPanel::_on_template_download_progress))) {
			template_manager->connect("template_download_progress", callable_mp(this, &SettingsPanel::_on_template_download_progress));
		}
		if (template_manager && !template_manager->is_connected("template_download_finished", callable_mp(this, &SettingsPanel::_on_template_cache_download_finished))) {
			template_manager->connect("template_download_finished", callable_mp(this, &SettingsPanel::_on_template_cache_download_finished));
		}
		if (template_manager && !template_manager->is_connected("active_template_changed", callable_mp(this, &SettingsPanel::_on_active_template_changed))) {
			template_manager->connect("active_template_changed", callable_mp(this, &SettingsPanel::_on_active_template_changed));
		}
		if (template_manager && !template_manager->is_connected("template_inventory_changed", callable_mp(this, &SettingsPanel::refresh_template_cache_info))) {
			template_manager->connect("template_inventory_changed", callable_mp(this, &SettingsPanel::refresh_template_cache_info));
		}
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

	call_deferred("refresh_distribution_info");
	call_deferred("refresh_template_cache_info");
}

void SettingsPanel::create_interface() {
	content_control = memnew(Control);
	content_control->set_anchors_preset(Control::PRESET_FULL_RECT);
	add_child(content_control);

	main_vbox = memnew(VBoxContainer);
	main_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content_control->add_child(main_vbox);

	plugin_update_row = memnew(HBoxContainer);
	main_vbox->add_child(plugin_update_row);

	plugin_update_label = memnew(Label);
	plugin_update_label->set_text(String::utf8("插件更新：尚未检查"));
	plugin_update_label->set_custom_minimum_size(Vector2(0, 30));
	plugin_update_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	plugin_update_row->add_child(plugin_update_label);

	check_plugin_update_button = memnew(Button);
	check_plugin_update_button->set_text(String::utf8("检查插件更新"));
	check_plugin_update_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_check_plugin_update_pressed));
	plugin_update_row->add_child(check_plugin_update_button);

	download_plugin_update_button = memnew(Button);
	download_plugin_update_button->set_text(String::utf8("下载插件更新"));
	download_plugin_update_button->set_disabled(true);
	download_plugin_update_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_download_plugin_update_pressed));
	plugin_update_row->add_child(download_plugin_update_button);

	update_restart_dialog = memnew(ConfirmationDialog);
	update_restart_dialog->set_title(String::utf8("安装插件更新"));
	update_restart_dialog->set_text(String::utf8("将保存当前场景和项目设置，关闭 Godot，安装已下载的插件更新，然后重新打开当前项目。是否继续？"));
	update_restart_dialog->get_ok_button()->set_text(String::utf8("安装并重启"));
	update_restart_dialog->connect("confirmed", callable_mp(this, &SettingsPanel::_on_confirm_update_restart));
	add_child(update_restart_dialog);

	distribution_provider_row = memnew(HBoxContainer);
	distribution_provider_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(distribution_provider_row);

	distribution_provider_title_label = memnew(Label);
	distribution_provider_title_label->set_text(String::utf8("模板分发源"));
	distribution_provider_title_label->set_custom_minimum_size(Vector2(72, 30));
	distribution_provider_row->add_child(distribution_provider_title_label);

	distribution_provider_selector = memnew(OptionButton);
	distribution_provider_selector->set_custom_minimum_size(Vector2(120, 0));
	distribution_provider_selector->add_item("AtomGit");
	distribution_provider_selector->add_item("GitHub");
	distribution_provider_selector->add_item("Gitee");
	distribution_provider_selector->connect("item_selected", callable_mp(this, &SettingsPanel::_on_distribution_provider_selected));
	distribution_provider_row->add_child(distribution_provider_selector);

	Control *distribution_provider_spacer = memnew(Control);
	distribution_provider_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	distribution_provider_row->add_child(distribution_provider_spacer);

	save_config_button = memnew(Button);
	save_config_button->set_text(String::utf8("保存并刷新"));
	save_config_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_save_distribution_config_pressed));
	distribution_provider_row->add_child(save_config_button);

	reset_config_button = memnew(Button);
	reset_config_button->set_text(String::utf8("恢复默认配置"));
	reset_config_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_reset_config_pressed));
	distribution_provider_row->add_child(reset_config_button);

	refresh_versions_button = memnew(Button);
	refresh_versions_button->set_text(String::utf8("刷新远端索引"));
	refresh_versions_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_refresh_versions_pressed));
	distribution_provider_row->add_child(refresh_versions_button);

	Control *distribution_repo_gap = memnew(Control);
	distribution_repo_gap->set_custom_minimum_size(Vector2(0, 5));
	main_vbox->add_child(distribution_repo_gap);

	release_config_row = memnew(HBoxContainer);
	release_config_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(release_config_row);

	owner_label = memnew(Label);
	owner_label->set_text("Owner");
	owner_label->set_custom_minimum_size(Vector2(48, 30));
	release_config_row->add_child(owner_label);

	owner_input = memnew(LineEdit);
	owner_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	owner_input->set_custom_minimum_size(Vector2(120, 0));
	owner_input->set_stretch_ratio(1.2f);
	owner_input->set_placeholder("Losomz");
	release_config_row->add_child(owner_input);

	repo_label = memnew(Label);
	repo_label->set_text("Repo");
	repo_label->set_custom_minimum_size(Vector2(44, 30));
	release_config_row->add_child(repo_label);

	repo_input = memnew(LineEdit);
	repo_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	repo_input->set_custom_minimum_size(Vector2(160, 0));
	repo_input->set_stretch_ratio(1.6f);
	repo_input->set_placeholder("godot-minigame");
	release_config_row->add_child(repo_input);

	tag_label = memnew(Label);
	tag_label->set_text("Tag");
	tag_label->set_custom_minimum_size(Vector2(36, 30));
	release_config_row->add_child(tag_label);

	tag_input = memnew(LineEdit);
	tag_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tag_input->set_custom_minimum_size(Vector2(120, 0));
	tag_input->set_stretch_ratio(1.0f);
	tag_input->set_placeholder("latest / 4.5.1");
	release_config_row->add_child(tag_input);

	action_status_label = memnew(Label);
	action_status_label->set_text(String::utf8("配置已就绪"));
	action_status_label->set_custom_minimum_size(Vector2(0, 30));
	main_vbox->add_child(action_status_label);

	Control *template_cache_gap = memnew(Control);
	template_cache_gap->set_custom_minimum_size(Vector2(0, 5));
	main_vbox->add_child(template_cache_gap);

	HBoxContainer *template_selection_row = memnew(HBoxContainer);
	template_selection_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(template_selection_row);

	Label *template_selection_label = memnew(Label);
	template_selection_label->set_text(String::utf8("当前模板"));
	template_selection_label->set_custom_minimum_size(Vector2(72, 30));
	template_selection_row->add_child(template_selection_label);

	template_version_selector = memnew(OptionButton);
	template_version_selector->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	template_version_selector->connect("item_selected", callable_mp(this, &SettingsPanel::_on_template_version_selected));
	template_selection_row->add_child(template_version_selector);

	HBoxContainer *custom_template_row = memnew(HBoxContainer);
	custom_template_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(custom_template_row);

	Label *custom_template_label = memnew(Label);
	custom_template_label->set_text(String::utf8("自定义 TPZ"));
	custom_template_label->set_custom_minimum_size(Vector2(72, 30));
	custom_template_row->add_child(custom_template_label);

	custom_template_url_input = memnew(LineEdit);
	custom_template_url_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	custom_template_url_input->set_placeholder("https://.../template.tpz");
	custom_template_row->add_child(custom_template_url_input);

	use_custom_template_button = memnew(Button);
	use_custom_template_button->set_text(String::utf8("设为当前"));
	use_custom_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_use_custom_template_pressed));
	custom_template_row->add_child(use_custom_template_button);

	template_cache_row = memnew(HBoxContainer);
	template_cache_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(template_cache_row);

	template_cache_label = memnew(Label);
	template_cache_label->set_text(String::utf8("模板缓存："));
	template_cache_label->set_custom_minimum_size(Vector2(0, 30));
	template_cache_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	template_cache_row->add_child(template_cache_label);

	prefetch_template_button = memnew(Button);
	prefetch_template_button->set_text(String::utf8("下载"));
	prefetch_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_prefetch_template_pressed));
	template_cache_row->add_child(prefetch_template_button);

	replace_template_button = memnew(Button);
	replace_template_button->set_text(String::utf8("重新下载并覆盖"));
	replace_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_replace_template_pressed));
	template_cache_row->add_child(replace_template_button);

	remove_template_button = memnew(Button);
	remove_template_button->set_text(String::utf8("删除当前缓存"));
	remove_template_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_remove_template_pressed));
	template_cache_row->add_child(remove_template_button);

	clear_templates_button = memnew(Button);
	clear_templates_button->set_text(String::utf8("清空全部"));
	clear_templates_button->connect("pressed", callable_mp(this, &SettingsPanel::_on_clear_templates_pressed));
	template_cache_row->add_child(clear_templates_button);

	template_cache_progress = memnew(ProgressBar);
	template_cache_progress->set_custom_minimum_size(Vector2(0, 6));
	template_cache_progress->set_min(0.0);
	template_cache_progress->set_max(1.0);
	template_cache_progress->set_value(0.0);
	template_cache_progress->set_show_percentage(false);
	template_cache_progress->set_visible(false);
	main_vbox->add_child(template_cache_progress);

	clear_templates_dialog = memnew(ConfirmationDialog);
	clear_templates_dialog->set_title(String::utf8("清空全局模板缓存"));
	clear_templates_dialog->set_text(String::utf8("将删除所有项目共享的已下载模板。此操作不会修改模板分发源。"));
	clear_templates_dialog->get_ok_button()->set_text(String::utf8("清空"));
	clear_templates_dialog->connect("confirmed", callable_mp(this, &SettingsPanel::_on_confirm_clear_templates));
	add_child(clear_templates_dialog);

	Control *spacer = memnew(Control);
	spacer->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(spacer);
}

void SettingsPanel::_on_check_plugin_update_pressed() {
	UpdateManager *update_manager = UpdateManager::get_singleton();
	if (!update_manager) {
		_on_plugin_update_error("Plugin update manager is unavailable.");
		return;
	}
	Ref<ConfigFile> config;
	config.instantiate();
	String version = "0.0.0";
	if (config->load("res://addons/godot-minigame/plugin.cfg") == OK) {
		version = String(config->get_value("plugin", "version", version)).strip_edges();
	}
	plugin_update_label->set_text(String::utf8("插件更新：正在检查，当前版本 ") + version);
	check_plugin_update_button->set_disabled(true);
	download_plugin_update_button->set_disabled(true);
	update_manager->check_for_updates(version);
}

void SettingsPanel::_on_download_plugin_update_pressed() {
	UpdateManager *update_manager = UpdateManager::get_singleton();
	if (!update_manager) {
		_on_plugin_update_error("Plugin update manager is unavailable.");
		return;
	}
	if (update_manager->get_current_state() == UpdateManager::STATE_DOWNLOADED) {
		if (update_restart_dialog) {
			update_restart_dialog->popup_centered();
		}
		return;
	}
	download_plugin_update_button->set_disabled(true);
	update_manager->download_update();
}

void SettingsPanel::_on_plugin_update_state_changed(int state) {
	if (check_plugin_update_button) {
		check_plugin_update_button->set_disabled(state == UpdateManager::STATE_CHECKING || state == UpdateManager::STATE_DOWNLOADING || state == UpdateManager::STATE_DOWNLOADED || state == UpdateManager::STATE_INSTALLING);
	}
	if (download_plugin_update_button) {
		download_plugin_update_button->set_text(state == UpdateManager::STATE_DOWNLOADED ? String::utf8("安装并重启") : String::utf8("下载插件更新"));
		download_plugin_update_button->set_disabled(state != UpdateManager::STATE_UPDATE_AVAILABLE && state != UpdateManager::STATE_DOWNLOADED);
	}
	if (plugin_update_label) {
		if (state == UpdateManager::STATE_UP_TO_DATE) {
			plugin_update_label->set_text(String::utf8("插件更新：当前已是最新版本"));
		} else if (state == UpdateManager::STATE_DOWNLOADING) {
			plugin_update_label->set_text(String::utf8("插件更新：正在下载"));
		} else if (state == UpdateManager::STATE_DOWNLOADED) {
			plugin_update_label->set_text(String::utf8("插件更新已下载，等待确认安装"));
		} else if (state == UpdateManager::STATE_INSTALLING) {
			plugin_update_label->set_text(String::utf8("正在启动安装程序并重启 Godot..."));
		}
	}
}

void SettingsPanel::_on_plugin_update_available(const Dictionary &version_info) {
	String version = String(version_info.get("version", ""));
	plugin_update_label->set_text(String::utf8("插件更新：发现新版本 ") + version);
	download_plugin_update_button->set_disabled(false);
}

void SettingsPanel::_on_plugin_update_download_finished(bool success) {
	if (success) {
		plugin_update_label->set_text(String::utf8("插件更新已下载，等待确认安装"));
		download_plugin_update_button->set_text(String::utf8("安装并重启"));
		download_plugin_update_button->set_disabled(false);
	} else {
		plugin_update_label->set_text(String::utf8("插件更新下载失败"));
	}
}

void SettingsPanel::_on_confirm_update_restart() {
	UpdateManager *update_manager = UpdateManager::get_singleton();
	if (!update_manager) {
		_on_plugin_update_error("Plugin update manager is unavailable.");
		return;
	}
	plugin_update_label->set_text(String::utf8("正在准备安装并重启 Godot..."));
	check_plugin_update_button->set_disabled(true);
	download_plugin_update_button->set_disabled(true);
	update_manager->restart_editor_for_update();
}

void SettingsPanel::_on_plugin_update_error(const String &message) {
	if (plugin_update_label) {
		plugin_update_label->set_text(String::utf8("插件更新失败：") + message);
	}
	if (check_plugin_update_button) {
		check_plugin_update_button->set_disabled(false);
	}
}

void SettingsPanel::refresh_distribution_info() {
	String provider = "github";
	Dictionary config;

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager) {
			if (template_manager->has_method("get_distribution_provider")) {
				provider = template_manager->call("get_distribution_provider");
			}

			if (template_manager->has_method("get_current_release_config")) {
				config = template_manager->call("get_current_release_config");
			}
		}
	}

	if (distribution_provider_selector) {
		if (provider == "gitee") {
			distribution_provider_selector->select(2);
		} else if (provider == "github") {
			distribution_provider_selector->select(1);
		} else {
			distribution_provider_selector->select(0);
		}
	}
	if (owner_input) {
		owner_input->set_text(String(config.get("owner", "")));
	}
	if (repo_input) {
		repo_input->set_text(String(config.get("repo", "")));
	}
	if (tag_input) {
		tag_input->set_text(String(config.get("release_tag", "latest")));
	}
}

void SettingsPanel::_on_distribution_provider_selected(int index) {
	String provider = "atomgit";
	if (index == 1) {
		provider = "github";
	} else if (index == 2) {
		provider = "gitee";
	}

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && template_manager->has_method("set_distribution_provider")) {
			template_manager->call("set_distribution_provider", provider);
		}
	}

	if (action_status_label) {
		action_status_label->set_text(String::utf8("已切换源，正在刷新索引..."));
	}
	refresh_distribution_info();
}

void SettingsPanel::_on_save_distribution_config_pressed() {
	if (!owner_input || !repo_input || !tag_input) {
		return;
	}

	String owner = owner_input->get_text().strip_edges();
	String repo = repo_input->get_text().strip_edges();
	String release_tag = tag_input->get_text().strip_edges();

	if (owner.is_empty() || repo.is_empty()) {
		if (action_status_label) {
			action_status_label->set_text(String::utf8("owner 和 repo 不能为空"));
		}
		return;
	}
	if (release_tag.is_empty()) {
		release_tag = "latest";
		tag_input->set_text(release_tag);
	}

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && template_manager->has_method("set_current_release_config")) {
			template_manager->call("set_current_release_config", owner, repo, release_tag);
		}
	}

	if (action_status_label) {
		action_status_label->set_text(String::utf8("配置已保存，正在刷新索引..."));
	}
	refresh_distribution_info();
}

void SettingsPanel::_on_reset_config_pressed() {
	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && template_manager->has_method("reset_distribution_preferences")) {
			template_manager->call("reset_distribution_preferences");
		}
	}

	if (action_status_label) {
		action_status_label->set_text(String::utf8("已恢复默认配置"));
	}
	refresh_distribution_info();
}

void SettingsPanel::_on_refresh_versions_pressed() {
	int error_code = ERR_UNCONFIGURED;

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && template_manager->has_method("refresh_versions")) {
			Variant result = template_manager->call("refresh_versions");
			if (result.get_type() == Variant::INT) {
				error_code = int(result);
			}
		}
	}

	if (action_status_label) {
		if (error_code == OK) {
			action_status_label->set_text(String::utf8("已发起远端索引刷新"));
		} else {
			action_status_label->set_text(String::utf8("刷新启动失败，错误码: ") + String::num_int64(error_code));
		}
	}
	refresh_distribution_info();
}

void SettingsPanel::_on_versions_loaded() {
	if (action_status_label) {
		action_status_label->set_text(String::utf8("远端索引刷新完成"));
	}
	refresh_distribution_info();
	refresh_template_cache_info();
}

void SettingsPanel::_on_versions_refresh_failed(int error_code) {
	if (action_status_label) {
		action_status_label->set_text(String::utf8("远端索引刷新失败，已保留当前索引。错误码: ") + String::num_int64(error_code));
	}
	refresh_distribution_info();
}

void SettingsPanel::_on_active_template_changed(const Dictionary &) {
	refresh_template_cache_info();
}

void SettingsPanel::_on_template_version_selected(int index) {
	if (!template_version_selector || index < 0 || index >= template_version_selector->get_item_count() ||
			!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	const String version = String(template_version_selector->get_item_metadata(index));
	Variant result = template_manager->call("set_active_catalog_template", version);
	if (action_status_label) {
		action_status_label->set_text(int(result) == OK ? String::utf8("当前模板已切换为 Godot ") + version : String::utf8("模板切换失败"));
	}
	refresh_template_cache_info();
}

void SettingsPanel::_on_use_custom_template_pressed() {
	if (!custom_template_url_input || !Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	Variant result = template_manager->call("set_active_custom_template", custom_template_url_input->get_text());
	if (int(result) != OK) {
		if (action_status_label) {
			action_status_label->set_text(String::utf8("自定义模板必须是 HTTP(S) 的 .tpz 直链"));
		}
		return;
	}
	_on_prefetch_template_pressed();
}

void SettingsPanel::_on_replace_template_pressed() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	Variant result = template_manager->call("download_active_template_async", true);
	if (action_status_label) {
		action_status_label->set_text(int(result) == OK ? String::utf8("正在重新下载，校验完成后将覆盖全局缓存") : String::utf8("无法启动覆盖下载"));
	}
	refresh_template_cache_info();
}

void SettingsPanel::_on_remove_template_pressed() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	Variant result = template_manager->call("remove_active_template_cache");
	if (action_status_label) {
		action_status_label->set_text(int(result) == OK ? String::utf8("当前模板缓存已删除") : String::utf8("当前模板缓存删除失败"));
	}
	refresh_template_cache_info();
}

void SettingsPanel::_on_clear_templates_pressed() {
	if (clear_templates_dialog) {
		clear_templates_dialog->popup_centered();
	}
}

void SettingsPanel::_on_confirm_clear_templates() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	Variant result = template_manager->call("clear_all_template_cache");
	if (action_status_label) {
		action_status_label->set_text(int(result) == OK ? String::utf8("全部全局模板缓存已清空") : String::utf8("全局模板缓存清理失败"));
	}
	refresh_template_cache_info();
}

void SettingsPanel::refresh_template_cache_info() {
	bool prefetch_active = false;
	Dictionary active_info;
	Array choices;

	if (Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager) {
			if (template_manager->has_method("get_template_choices")) {
				choices = template_manager->call("get_template_choices");
			}
			if (template_manager->has_method("get_active_template_info")) {
				active_info = template_manager->call("get_active_template_info");
			}
			if (template_manager->has_method("is_prefetch_active")) {
				prefetch_active = bool(template_manager->call("is_prefetch_active"));
			}
		}
	}

	if (template_version_selector) {
		template_version_selector->clear();
		const String active_kind = String(active_info.get("kind", "catalog"));
		const String active_version = String(active_info.get("version", ""));
		for (int i = 0; i < choices.size(); i++) {
			Dictionary choice = choices[i];
			const String version = String(choice.get("version", ""));
			const String tag = String(choice.get("release_tag", ""));
			template_version_selector->add_item("Godot " + version + (tag.is_empty() ? String() : "  [" + tag + "]"));
			template_version_selector->set_item_metadata(i, version);
			if (active_kind == "catalog" && version == active_version) {
				template_version_selector->select(i);
			}
		}
		template_version_selector->set_disabled(prefetch_active || choices.is_empty());
	}

	const String display_name = String(active_info.get("display_name", String::utf8("未选择模板")));
	if (custom_template_url_input && String(active_info.get("kind", "catalog")) == "custom") {
		custom_template_url_input->set_text(String(active_info.get("url", "")));
	}
	const bool cached = bool(active_info.get("cached", false));
	const bool bundled = bool(active_info.get("bundled", false));
	const bool available = bool(active_info.get("available", false));
	if (template_cache_label) {
		String status = cached ? String::utf8("全局缓存") : (bundled ? String::utf8("插件内置") : String::utf8("未下载"));
		template_cache_label->set_text(display_name + String::utf8("：") + status);
	}
	if (prefetch_template_button) {
		prefetch_template_button->set_disabled(prefetch_active || available || active_info.is_empty());
	}
	if (replace_template_button) {
		replace_template_button->set_disabled(prefetch_active || active_info.is_empty());
	}
	if (remove_template_button) {
		remove_template_button->set_disabled(prefetch_active || !cached);
	}
	if (clear_templates_button) {
		clear_templates_button->set_disabled(prefetch_active);
	}
	if (template_cache_progress) {
		template_cache_progress->set_visible(prefetch_active);
	}
}

void SettingsPanel::_on_prefetch_template_pressed() {
	if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
		return;
	}
	Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
	if (!template_manager || !template_manager->has_method("download_active_template_async")) {
		return;
	}

	Variant result = template_manager->call("download_active_template_async", false);
	int error_code = ERR_UNCONFIGURED;
	if (result.get_type() == Variant::INT) {
		error_code = int(result);
	}

	if (action_status_label) {
		if (error_code == OK) {
			action_status_label->set_text(String::utf8("模板下载已启动"));
		} else if (error_code == ERR_BUSY) {
			action_status_label->set_text(String::utf8("已有下载任务在进行"));
		} else {
			action_status_label->set_text(String::utf8("预下载启动失败，错误码: ") + String::num_int64(error_code));
		}
	}
	refresh_template_cache_info();
}

void SettingsPanel::_on_template_download_progress(const String &filename, float progress) {
	if (!template_cache_progress || !template_cache_progress->is_visible()) {
		return;
	}
	template_cache_progress->set_value(progress);
	if (template_cache_label && Engine::get_singleton()->has_singleton("TemplateManager")) {
		Object *template_manager = Engine::get_singleton()->get_singleton("TemplateManager");
		if (template_manager && template_manager->has_method("get_download_status_text") && !filename.is_empty()
				&& filename != String("__prefetch__")) {
			String status_text = template_manager->call("get_download_status_text", filename);
			template_cache_label->set_text(
					String::utf8("模板缓存：") + filename + String::utf8("（") + status_text + String::utf8("）"));
		}
	}
}

void SettingsPanel::_on_template_cache_download_finished(const String &filename, bool success) {
	if (template_cache_progress) {
		template_cache_progress->set_visible(false);
		template_cache_progress->set_value(0.0);
	}
	if (action_status_label) {
		if (success) {
			action_status_label->set_text(String::utf8("模板已写入全局缓存，当前编辑器可立即使用"));
		} else if (filename == String("__prefetch__")) {
			action_status_label->set_text(String::utf8("模板预下载失败：没有可用的模板版本"));
		} else {
			action_status_label->set_text(String::utf8("模板预下载失败，请检查分发源配置或网络后重试"));
		}
	}
	refresh_template_cache_info();
}

} // namespace editor
} // namespace toolkit
