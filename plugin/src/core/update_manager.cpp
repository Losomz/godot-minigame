#include "core/update_manager.h"
#include "core/logging.h"
#include "core/types.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_paths.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace toolkit {
namespace {

Error remove_tree(const String &path) {
    if (!DirAccess::dir_exists_absolute(path)) {
        return OK;
    }
    Ref<DirAccess> dir = DirAccess::open(path);
    if (dir.is_null()) {
        return ERR_CANT_OPEN;
    }
    dir->list_dir_begin();
    while (true) {
        const String name = dir->get_next();
        if (name.is_empty()) {
            break;
        }
        if (name == "." || name == "..") {
            continue;
        }
        const String child = path.path_join(name);
        const Error err = dir->current_is_dir() ? remove_tree(child) : DirAccess::remove_absolute(child);
        if (err != OK) {
            dir->list_dir_end();
            return err;
        }
    }
    dir->list_dir_end();
    return DirAccess::remove_absolute(path);
}

bool safe_relative_path(const String &path) {
    if (path.is_empty() || path.is_absolute_path() || path.contains(":") || path.contains("\\")) {
        return false;
    }
    PackedStringArray parts = path.split("/", false);
    for (int i = 0; i < parts.size(); i++) {
        if (parts[i] == ".." || parts[i] == ".") {
            return false;
        }
    }
    return true;
}

bool write_bytes(const String &path, const PackedByteArray &data) {
    DirAccess::make_dir_recursive_absolute(path.get_base_dir());
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
    if (file.is_null()) {
        return false;
    }
    file->store_buffer(data);
    file->close();
    return true;
}

bool write_text(const String &path, const String &text) {
    return write_bytes(path, text.to_utf8_buffer());
}

String platform_asset_key() {
    const String platform = OS::get_singleton()->get_name().to_lower();
    return platform == "macos" ? String("macos-universal") : platform + String("-") + Engine::get_singleton()->get_architecture_name();
}

} // namespace

UpdateManager *UpdateManager::singleton = nullptr;

UpdateManager *UpdateManager::get_singleton() {
    return singleton;
}

UpdateManager::UpdateManager() {
    if (!singleton) {
        singleton = this;
    }
    version_checker = nullptr;
    downloader = nullptr;
}

UpdateManager::~UpdateManager() {
    cleanup_http_nodes();
    if (singleton == this) {
        singleton = nullptr;
    }
}

void UpdateManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &UpdateManager::initialize);
    ClassDB::bind_method(D_METHOD("check_for_updates", "local_version"), &UpdateManager::check_for_updates);
    ClassDB::bind_method(D_METHOD("download_update"), &UpdateManager::download_update);
    ClassDB::bind_method(D_METHOD("cancel_download"), &UpdateManager::cancel_download);
    ClassDB::bind_method(D_METHOD("restart_editor_for_update"), &UpdateManager::restart_editor_for_update);
    ClassDB::bind_method(D_METHOD("get_local_version"), &UpdateManager::get_local_version);
    ClassDB::bind_method(D_METHOD("get_remote_version_info"), &UpdateManager::get_remote_version_info);
    ClassDB::bind_method(D_METHOD("get_current_state"), &UpdateManager::get_current_state);
    ClassDB::bind_method(D_METHOD("get_download_file_path"), &UpdateManager::get_download_file_path);
    ClassDB::bind_method(D_METHOD("get_last_install_message"), &UpdateManager::get_last_install_message);
    ClassDB::bind_method(D_METHOD("_on_version_check_completed"), &UpdateManager::_on_version_check_completed);
    ClassDB::bind_method(D_METHOD("_on_download_completed"), &UpdateManager::_on_download_completed);

    ADD_SIGNAL(MethodInfo("update_available", PropertyInfo(Variant::DICTIONARY, "version_info")));
    ADD_SIGNAL(MethodInfo("download_finished", PropertyInfo(Variant::BOOL, "success")));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("update_state_changed", PropertyInfo(Variant::INT, "new_state")));

    BIND_ENUM_CONSTANT(STATE_IDLE);
    BIND_ENUM_CONSTANT(STATE_CHECKING);
    BIND_ENUM_CONSTANT(STATE_UPDATE_AVAILABLE);
    BIND_ENUM_CONSTANT(STATE_DOWNLOADING);
    BIND_ENUM_CONSTANT(STATE_DOWNLOADED);
    BIND_ENUM_CONSTANT(STATE_INSTALLING);
    BIND_ENUM_CONSTANT(STATE_UP_TO_DATE);
    BIND_ENUM_CONSTANT(STATE_ERROR);
}

void UpdateManager::initialize() {
    load_last_install_result();
    if (version_checker && version_checker->get_parent()) {
        return;
    }
    Node *parent = nullptr;
    EditorInterface *editor = EditorInterface::get_singleton();
    if (editor) {
        parent = editor->get_editor_main_screen();
    }
    if (!parent) {
        parent = Object::cast_to<Node>(Engine::get_singleton()->get_main_loop());
    }
    if (!parent) {
        call_deferred("initialize");
        return;
    }
    if (!version_checker) {
        version_checker = memnew(HTTPRequest);
        version_checker->set_name("GodotMinigameUpdateCheck");
        version_checker->connect("request_completed", callable_mp(this, &UpdateManager::_on_version_check_completed));
    }
    if (!downloader) {
        downloader = memnew(HTTPRequest);
        downloader->set_name("GodotMinigameUpdateDownload");
        downloader->connect("request_completed", callable_mp(this, &UpdateManager::_on_download_completed));
    }
    if (!version_checker->get_parent()) {
        parent->add_child(version_checker);
        track_http_node(version_checker);
    }
    if (!downloader->get_parent()) {
        parent->add_child(downloader);
        track_http_node(downloader);
    }
}

void UpdateManager::check_for_updates(const String &p_local_version) {
    if (current_state == STATE_CHECKING || current_state == STATE_DOWNLOADING || current_state == STATE_INSTALLING) {
        return;
    }
    initialize();
    if (!version_checker || !version_checker->get_parent()) {
        set_state(STATE_ERROR);
        emit_signal("error", "Update manager is unavailable.");
        return;
    }
    local_version = p_local_version;
    set_state(STATE_CHECKING);
    version_checker->set_timeout(30.0);
    const Error err = version_checker->request(resolve_update_manifest_url());
    if (err != OK) {
        set_state(STATE_ERROR);
        emit_signal("error", "Failed to start update check: " + String::num_int64(err));
    }
}

void UpdateManager::_on_version_check_completed(int result, int response_code, const PackedStringArray &, const PackedByteArray &body) {
    if (result != HTTPRequest::RESULT_SUCCESS || response_code != 200) {
        set_state(STATE_ERROR);
        emit_signal("error", "Update check failed with HTTP status " + String::num_int64(response_code));
        return;
    }
    const Variant parsed = JSON::parse_string(body.get_string_from_utf8());
    if (parsed.get_type() != Variant::DICTIONARY) {
        set_state(STATE_ERROR);
        emit_signal("error", "Update manifest is invalid.");
        return;
    }
    remote_version_info = parsed;
    const String remote_version = String(remote_version_info.get("version", "")).strip_edges();
    const VersionInfo local = VersionInfo::from_string(local_version);
    const VersionInfo remote = VersionInfo::from_string(remote_version);
    if (bool(remote_version_info.get("published", false)) && !remote_version.is_empty() && remote.is_newer_than(local)) {
        set_state(STATE_UPDATE_AVAILABLE);
        emit_signal("update_available", remote_version_info);
    } else {
        set_state(STATE_UP_TO_DATE);
    }
}

void UpdateManager::download_update() {
    if (current_state != STATE_UPDATE_AVAILABLE) {
        return;
    }
    initialize();
    Dictionary platforms = remote_version_info.get("platforms", Dictionary());
    const String key = platform_asset_key();
    if (!platforms.has(key)) {
        set_state(STATE_ERROR);
        emit_signal("error", "No update package for " + key);
        return;
    }
    const Dictionary platform = platforms[key];
    const String asset = resolve_platform_asset_name(platform);
    const String url = resolve_update_asset_url(platform);
    expected_download_sha256 = String(platform.get("sha256", "")).strip_edges().to_lower();
    if (asset.is_empty() || url.is_empty() || expected_download_sha256.length() != 64) {
        set_state(STATE_ERROR);
        emit_signal("error", "Update asset metadata is incomplete.");
        return;
    }
    download_file_path = get_update_cache_root().path_join(asset);
    DirAccess::make_dir_recursive_absolute(download_file_path.get_base_dir());
    downloader->set_download_file(download_file_path);
    downloader->set_timeout(60.0);
    set_state(STATE_DOWNLOADING);
    const Error err = downloader->request(url);
    if (err != OK) {
        set_state(STATE_ERROR);
        emit_signal("error", "Failed to start update download: " + String::num_int64(err));
    }
}

void UpdateManager::_on_download_completed(int result, int response_code, const PackedStringArray &, const PackedByteArray &) {
    if (result != HTTPRequest::RESULT_SUCCESS || response_code != 200 || !FileAccess::file_exists(download_file_path)) {
        set_state(STATE_ERROR);
        emit_signal("download_finished", false);
        emit_signal("error", "Plugin update download failed.");
        return;
    }
    if (FileAccess::get_sha256(download_file_path).to_lower() != expected_download_sha256) {
        DirAccess::remove_absolute(download_file_path);
        set_state(STATE_ERROR);
        emit_signal("download_finished", false);
        emit_signal("error", "Downloaded plugin package failed SHA-256 verification.");
        return;
    }
    set_state(STATE_DOWNLOADED);
    emit_signal("download_finished", true);
}

String UpdateManager::get_update_cache_root() const {
    EditorInterface *editor = EditorInterface::get_singleton();
    String root;
    if (editor && editor->get_editor_paths()) {
        root = editor->get_editor_paths()->get_cache_dir().path_join("godot-minigame/updates");
    } else {
        root = OS::get_singleton()->get_cache_dir().path_join("godot-minigame/updates");
    }
    DirAccess::make_dir_recursive_absolute(root);
    return root;
}

bool UpdateManager::prepare_update_and_restart(String &r_error) {
    if (current_state != STATE_DOWNLOADED || !FileAccess::file_exists(download_file_path)) {
        r_error = "No verified update package is ready.";
        return false;
    }
    const String version = String(remote_version_info.get("version", "")).strip_edges();
    if (version.is_empty() || version.contains("/") || version.contains("\\") || version.contains(":")) {
        r_error = "Update version is invalid.";
        return false;
    }

    Ref<ZIPReader> zip;
    zip.instantiate();
    if (zip->open(download_file_path) != OK) {
        r_error = "Update package cannot be opened.";
        return false;
    }
    const String stage_root = get_update_cache_root().path_join("staged").path_join(version);
    remove_tree(stage_root);
    const String staged_addon = stage_root.path_join("addons/godot-minigame");
    DirAccess::make_dir_recursive_absolute(staged_addon);
    const String prefix = "addons/godot-minigame/";
    PackedStringArray entries = zip->get_files();
    HashSet<String> seen;
    String plugin_config_text;
    bool has_descriptor = false;
    bool has_helper = false;
    bool has_native = false;
    const String native_platform = OS::get_singleton()->get_name().to_lower() == "macos" ? String("macos") : OS::get_singleton()->get_name().to_lower();
    for (int i = 0; i < entries.size(); i++) {
        const String archive_path = String(entries[i]).replace("\\", "/");
        if (!archive_path.begins_with(prefix)) {
            zip->close();
            r_error = "Update package contains files outside the plugin directory.";
            remove_tree(stage_root);
            return false;
        }
        const String relative = archive_path.substr(prefix.length());
        if (relative.is_empty() || archive_path.ends_with("/")) {
            continue;
        }
        if (!safe_relative_path(relative) || seen.has(relative.to_lower())) {
            zip->close();
            r_error = "Update package contains an unsafe or duplicate path: " + relative;
            remove_tree(stage_root);
            return false;
        }
        seen.insert(relative.to_lower());
        const PackedByteArray data = zip->read_file(entries[i]);
        if (!write_bytes(staged_addon.path_join(relative), data)) {
            zip->close();
            r_error = "Cannot stage update file: " + relative;
            remove_tree(stage_root);
            return false;
        }
        if (relative == "plugin.cfg") {
            plugin_config_text = data.get_string_from_utf8();
        } else if (relative == "godot-minigame.gdextension") {
            has_descriptor = true;
        } else if (relative == "update_helper.gd") {
            has_helper = true;
        } else if (relative.begins_with("bin/" + native_platform + "/")) {
            has_native = true;
        }
    }
    zip->close();

    Ref<ConfigFile> config;
    config.instantiate();
    if (config->parse(plugin_config_text) != OK || String(config->get_value("plugin", "version", "")) != version ||
            !has_descriptor || !has_helper || !has_native) {
        r_error = "Update package does not contain a complete matching plugin.";
        remove_tree(stage_root);
        return false;
    }

    const String helper_root = get_update_cache_root().path_join("helper");
    remove_tree(helper_root);
    DirAccess::make_dir_recursive_absolute(helper_root);
    const String helper_source = staged_addon.path_join("update_helper.gd");
    Ref<FileAccess> helper_file = FileAccess::open(helper_source, FileAccess::READ);
    if (helper_file.is_null()) {
        r_error = "Update helper is missing.";
        return false;
    }
    const PackedByteArray helper_data = helper_file->get_buffer(helper_file->get_length());
    helper_file->close();
    const String helper_script = helper_root.path_join("update_helper.gd");
    if (!write_bytes(helper_script, helper_data) || !write_text(helper_root.path_join("project.godot"), "[application]\nconfig/name=\"Godot Minigame Updater\"\n")) {
        r_error = "Cannot create update helper project.";
        return false;
    }

    Dictionary manifest;
    manifest["parent_pid"] = OS::get_singleton()->get_process_id();
    manifest["project_path"] = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
    manifest["addon_path"] = ProjectSettings::get_singleton()->globalize_path("res://addons/godot-minigame").trim_suffix("/");
    manifest["staged_addon_path"] = staged_addon;
    manifest["result_path"] = get_update_cache_root().path_join("last-install.json");
    manifest["editor_path"] = OS::get_singleton()->get_executable_path();
    manifest["version"] = version;
    manifest["relaunch"] = true;
    const String manifest_path = helper_root.path_join("manifest.json");
    if (!write_text(manifest_path, JSON::stringify(manifest, "  "))) {
        r_error = "Cannot write update manifest.";
        return false;
    }

    EditorInterface *editor = EditorInterface::get_singleton();
    if (!editor) {
        r_error = "Editor interface is unavailable.";
        return false;
    }
    editor->save_all_scenes();
    if (ProjectSettings::get_singleton()->save() != OK) {
        r_error = "Project settings could not be saved.";
        return false;
    }
    SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
    if (!tree) {
        r_error = "Editor shutdown interface is unavailable.";
        return false;
    }
    PackedStringArray args;
    args.append("--headless");
    args.append("--path");
    args.append(helper_root);
    args.append("--script");
    args.append(helper_script);
    args.append("--");
    args.append(manifest_path);
    if (OS::get_singleton()->create_process(OS::get_singleton()->get_executable_path(), args, false) <= 0) {
        r_error = "Update helper process could not be started.";
        return false;
    }
    set_state(STATE_INSTALLING);
    tree->quit();
    return true;
}

void UpdateManager::restart_editor_for_update() {
    String error;
    if (!prepare_update_and_restart(error)) {
        set_state(STATE_ERROR);
        emit_signal("error", error);
    }
}

void UpdateManager::load_last_install_result() {
    const String path = get_update_cache_root().path_join("last-install.json");
    if (!FileAccess::file_exists(path)) {
        return;
    }
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_valid()) {
        const Variant parsed = JSON::parse_string(file->get_as_text());
        file->close();
        if (parsed.get_type() == Variant::DICTIONARY) {
            Dictionary result = parsed;
            last_install_message = String(result.get("message", ""));
        }
    }
    DirAccess::remove_absolute(path);
}

bool UpdateManager::is_properly_configured() const {
    return !resolve_update_manifest_url().is_empty();
}

String UpdateManager::resolve_update_manifest_url() const {
    const String override_url = OS::get_singleton()->get_environment("GODOT_MINIGAME_PLUGIN_UPDATE_URL").strip_edges();
    return override_url.is_empty() ? String("https://raw.githubusercontent.com/Losomz/godot-minigame/main/plugin/catalog/plugin-stable.json") : override_url;
}

String UpdateManager::resolve_update_asset_url(const Dictionary &platform_data) const {
    const String url = String(platform_data.get("url", "")).strip_edges();
    return url.begins_with("https://") ? url : String();
}

String UpdateManager::resolve_platform_asset_name(const Dictionary &platform_data) const {
    return String(platform_data.get("asset", "")).strip_edges().get_file();
}

void UpdateManager::cancel_download() {
    if (current_state == STATE_DOWNLOADING && downloader) {
        downloader->cancel_request();
        set_state(STATE_UPDATE_AVAILABLE);
    }
}

void UpdateManager::set_state(UpdateState state) {
    if (current_state != state) {
        current_state = state;
        emit_signal("update_state_changed", current_state);
    }
}

void UpdateManager::track_http_node(Node *node) {
    if (node && !active_http_nodes.has(node)) {
        active_http_nodes.append(node);
    }
}

void UpdateManager::cleanup_http_nodes() {
    for (int i = 0; i < active_http_nodes.size(); i++) {
        Node *node = Object::cast_to<Node>(active_http_nodes[i]);
        if (node && node->get_parent()) {
            node->get_parent()->remove_child(node);
            node->queue_free();
        }
    }
    active_http_nodes.clear();
    version_checker = nullptr;
    downloader = nullptr;
}

UpdateManager::UpdateState UpdateManager::get_current_state() const { return current_state; }
String UpdateManager::get_download_file_path() const { return download_file_path; }
String UpdateManager::get_local_version() const { return local_version; }
Dictionary UpdateManager::get_remote_version_info() const { return remote_version_info; }
String UpdateManager::get_last_install_message() const { return last_install_message; }

} // namespace toolkit
