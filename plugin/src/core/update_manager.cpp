#include "core/update_manager.h"
#include "core/logging.h"
#include "core/plugin_state_store.h"
#include "core/types.h"

#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
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

#include <regex>

using namespace godot;

namespace toolkit {

namespace {

constexpr const char *PLUGIN_UPDATE_STATE_SECTION = "plugin_update";
constexpr const char *LEGACY_PLUGIN_UPDATE_CHANNEL_SETTING = "godot_minigame/plugin_update/channel";

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
    PackedStringArray parts = path.split("/", true);
    for (int i = 0; i < parts.size(); i++) {
        if (parts[i].is_empty() || parts[i] == ".." || parts[i] == ".") {
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

String platform_native_directory() {
    const String platform = OS::get_singleton()->get_name().to_lower();
    return platform == "macos" ? String("macos") : platform;
}

String platform_native_extension() {
    const String platform = platform_native_directory();
    if (platform == "windows") {
        return "dll";
    }
    if (platform == "macos") {
        return "dylib";
    }
    return platform == "linux" ? String("so") : String();
}

String platform_update_waiter_filename() {
    const String platform = platform_native_directory();
    if (platform == "windows") {
        return "godot-minigame-update-waiter.windows." + Engine::get_singleton()->get_architecture_name() + ".exe";
    }
    if (platform == "linux") {
        return "godot-minigame-update-waiter.linux." + Engine::get_singleton()->get_architecture_name();
    }
    return platform == "macos" ? String("godot-minigame-update-waiter.macos") : String();
}

bool valid_plugin_version(const String &version) {
    const std::regex pattern(R"(^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$)");
    return std::regex_match(std::string(version.utf8().get_data()), pattern);
}

uint16_t read_zip_u16(const PackedByteArray &data, int64_t offset) {
    return uint16_t(data[offset]) | (uint16_t(data[offset + 1]) << 8);
}

uint32_t read_zip_u32(const PackedByteArray &data, int64_t offset) {
    return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
            (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
}

bool validate_raw_zip_paths(const String &path, String &r_error) {
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_null() || file->get_length() < 22) {
        r_error = "Plugin package has an invalid ZIP directory.";
        return false;
    }
    const PackedByteArray data = file->get_buffer(file->get_length());
    file->close();

    int64_t eocd = -1;
    const int64_t search_start = data.size() > 65557 ? data.size() - 65557 : 0;
    for (int64_t offset = data.size() - 22; offset >= search_start; offset--) {
        if (read_zip_u32(data, offset) == 0x06054b50 && offset + 22 + read_zip_u16(data, offset + 20) == data.size()) {
            eocd = offset;
            break;
        }
    }
    if (eocd < 0) {
        r_error = "Plugin package has an invalid ZIP directory.";
        return false;
    }

    const uint16_t entry_count = read_zip_u16(data, eocd + 10);
    const uint32_t directory_size = read_zip_u32(data, eocd + 12);
    const uint32_t directory_offset = read_zip_u32(data, eocd + 16);
    if (entry_count == 0xffff || directory_size == 0xffffffff || directory_offset == 0xffffffff ||
            int64_t(directory_offset) + int64_t(directory_size) > eocd) {
        r_error = "Plugin package uses an unsupported or invalid ZIP directory.";
        return false;
    }

    int64_t offset = directory_offset;
    for (uint32_t index = 0; index < entry_count; index++) {
        if (offset + 46 > data.size() || read_zip_u32(data, offset) != 0x02014b50) {
            r_error = "Plugin package has an invalid ZIP entry.";
            return false;
        }
        const uint16_t name_length = read_zip_u16(data, offset + 28);
        const uint16_t extra_length = read_zip_u16(data, offset + 30);
        const uint16_t comment_length = read_zip_u16(data, offset + 32);
        const int64_t next_offset = offset + 46 + name_length + extra_length + comment_length;
        if (name_length == 0 || next_offset > data.size()) {
            r_error = "Plugin package has an invalid ZIP entry name.";
            return false;
        }
        for (uint16_t name_index = 0; name_index < name_length; name_index++) {
            if (data[offset + 46 + name_index] == '\\') {
                r_error = "Plugin package contains a backslash path.";
                return false;
            }
        }
        offset = next_offset;
    }
    if (offset != int64_t(directory_offset) + int64_t(directory_size)) {
        r_error = "Plugin package ZIP directory size is inconsistent.";
        return false;
    }
    return true;
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
    ClassDB::bind_method(D_METHOD("select_local_package", "path", "local_version"), &UpdateManager::select_local_package);
    ClassDB::bind_method(D_METHOD("set_update_channel", "channel"), &UpdateManager::set_update_channel);
    ClassDB::bind_method(D_METHOD("get_update_channel"), &UpdateManager::get_update_channel);
    ClassDB::bind_method(D_METHOD("get_last_local_package_path"), &UpdateManager::get_last_local_package_path);
    ClassDB::bind_method(D_METHOD("clear_pending_update"), &UpdateManager::clear_pending_update);
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
    load_plugin_state();
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

bool UpdateManager::validate_update_package(const String &p_path, String &r_version, String &r_error) const {
    r_version = String();
    r_error = String();
    if (!p_path.to_lower().ends_with(".zip") || !FileAccess::file_exists(p_path)) {
        r_error = "Select a plugin ZIP file.";
        return false;
    }
    if (!validate_raw_zip_paths(p_path, r_error)) {
        return false;
    }

    Ref<ZIPReader> zip;
    zip.instantiate();
    if (zip->open(p_path) != OK) {
        r_error = "Plugin package cannot be opened as ZIP.";
        return false;
    }

    const String prefix = "addons/godot-minigame/";
    const String native_prefix = "bin/" + platform_native_directory() + "/";
    const String native_extension = platform_native_extension();
    const PackedStringArray entries = zip->get_files();
    HashSet<String> seen;
    String plugin_config_text;
    bool has_descriptor = false;
    bool has_helper = false;
    bool has_native = false;

    for (int i = 0; i < entries.size(); i++) {
        const String archive_path = entries[i];
        const bool is_directory = archive_path.ends_with("/");
        const String canonical_path = is_directory ? archive_path.trim_suffix("/") : archive_path;
        if (archive_path.contains("\\") || canonical_path.is_empty() || canonical_path.is_absolute_path() ||
                canonical_path.contains(":") || !safe_relative_path(canonical_path) ||
                (canonical_path == prefix.trim_suffix("/") && !is_directory) ||
                (canonical_path != prefix.trim_suffix("/") && !archive_path.begins_with(prefix))) {
            zip->close();
            r_error = "Plugin package contains a path outside addons/godot-minigame: " + archive_path;
            return false;
        }
        const String path_key = canonical_path.to_lower();
        if (seen.has(path_key)) {
            zip->close();
            r_error = "Plugin package contains a duplicate path: " + archive_path;
            return false;
        }
        seen.insert(path_key);
        if (is_directory) {
            continue;
        }

        const String relative = archive_path.substr(prefix.length());
        if (relative == "plugin.cfg") {
            plugin_config_text = zip->read_file(entries[i]).get_string_from_utf8();
        } else if (relative == "godot-minigame.gdextension") {
            has_descriptor = true;
        } else if (relative == "update_helper.gd") {
            has_helper = true;
        } else if (!native_extension.is_empty() && relative.begins_with(native_prefix) && relative.get_extension().to_lower() == native_extension) {
            has_native = true;
        }
    }
    zip->close();

    Ref<ConfigFile> config;
    config.instantiate();
    if (plugin_config_text.is_empty() || config->parse(plugin_config_text) != OK) {
        r_error = "Plugin package is missing a valid plugin.cfg.";
        return false;
    }
    r_version = String(config->get_value("plugin", "version", "")).strip_edges();
    if (!valid_plugin_version(r_version)) {
        r_error = "Plugin package version must use semantic versioning.";
        return false;
    }
    if (!has_descriptor || !has_helper || !has_native) {
        r_error = "Plugin package is missing the extension descriptor, update helper, or current-platform native library.";
        return false;
    }
    return true;
}

Dictionary UpdateManager::select_local_package(const String &p_path, const String &p_local_version) {
    Dictionary result;
    result["success"] = false;

    const String source_path = p_path.simplify_path();
    if (source_path.is_absolute_path()) {
        last_local_package_path = source_path;
        persist_plugin_state();
    }
    String version;
    String error;
    if (!validate_update_package(source_path, version, error)) {
        result["error"] = error;
        return result;
    }

    clear_pending_update();
    const String source_sha256_before = FileAccess::get_sha256(source_path).to_lower();
    const String incoming_path = get_update_cache_root().path_join("local-package.incoming.zip");
    DirAccess::remove_absolute(incoming_path);
    if (source_sha256_before.length() != 64 || DirAccess::copy_absolute(source_path, incoming_path) != OK) {
        DirAccess::remove_absolute(incoming_path);
        result["error"] = "Cannot copy the plugin package into the editor update cache.";
        return result;
    }
    const String source_sha256_after = FileAccess::get_sha256(source_path).to_lower();
    const String cached_sha256 = FileAccess::get_sha256(incoming_path).to_lower();
    String cached_version;
    if (source_sha256_before != source_sha256_after || source_sha256_before != cached_sha256 ||
            !validate_update_package(incoming_path, cached_version, error) || cached_version != version) {
        DirAccess::remove_absolute(incoming_path);
        result["error"] = error.is_empty() ? String("Plugin package changed while it was being copied.") : error;
        return result;
    }

    const String local_root = get_update_cache_root().path_join("local").path_join(version);
    remove_tree(local_root);
    DirAccess::make_dir_recursive_absolute(local_root);
    download_file_path = local_root.path_join("package.zip");
    if (DirAccess::rename_absolute(incoming_path, download_file_path) != OK) {
        DirAccess::remove_absolute(incoming_path);
        download_file_path = String();
        result["error"] = "Cannot finalize the cached plugin package.";
        return result;
    }

    local_version = p_local_version.strip_edges();
    expected_download_sha256 = cached_sha256;
    remote_version_info.clear();
    remote_version_info["channel"] = "local";
    remote_version_info["version"] = version;
    remote_version_info["filename"] = source_path.get_file();
    remote_version_info["sha256"] = cached_sha256;
    set_state(STATE_DOWNLOADED);

    result["success"] = true;
    result["version"] = version;
    result["filename"] = source_path.get_file();
    result["sha256"] = cached_sha256;
    return result;
}

String UpdateManager::get_update_cache_root() const {
    const String root = PluginStateStore::get_root_dir().path_join("updates");
    DirAccess::make_dir_recursive_absolute(root);
    return root;
}

void UpdateManager::load_plugin_state() {
    if (plugin_state_loaded) {
        return;
    }
    plugin_state_loaded = true;

    const bool has_state = PluginStateStore::has_section(PLUGIN_UPDATE_STATE_SECTION);
    Dictionary state = PluginStateStore::load_section(PLUGIN_UPDATE_STATE_SECTION);
    if (!has_state) {
        EditorInterface *editor = EditorInterface::get_singleton();
        if (editor) {
            Ref<EditorSettings> settings = editor->get_editor_settings();
            if (settings.is_valid() && settings->has_setting(LEGACY_PLUGIN_UPDATE_CHANNEL_SETTING)) {
                update_channel = String(settings->get_setting(LEGACY_PLUGIN_UPDATE_CHANNEL_SETTING)).strip_edges().to_lower();
            }
        }
    } else {
        update_channel = String(state.get("channel", "remote")).strip_edges().to_lower();
        last_local_package_path = String(state.get("last_package_path", "")).simplify_path();
    }
    if (update_channel != "local") {
        update_channel = "remote";
    }
    persist_plugin_state();
}

void UpdateManager::persist_plugin_state() const {
    Dictionary state = PluginStateStore::load_section(PLUGIN_UPDATE_STATE_SECTION);
    state["channel"] = update_channel;
    state["last_package_path"] = last_local_package_path;
    PluginStateStore::save_section(PLUGIN_UPDATE_STATE_SECTION, state);
}

void UpdateManager::set_update_channel(const String &p_channel) {
    load_plugin_state();
    update_channel = p_channel.strip_edges().to_lower() == "local" ? String("local") : String("remote");
    persist_plugin_state();
}

String UpdateManager::get_update_channel() const {
    return update_channel;
}

String UpdateManager::get_last_local_package_path() const {
    return last_local_package_path;
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
    String verified_version;
    if (!validate_update_package(download_file_path, verified_version, r_error) || verified_version != version) {
        if (r_error.is_empty()) {
            r_error = "Update package version does not match the selected update.";
        }
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
    bool has_waiter = false;
    const String native_platform = platform_native_directory();
    const String native_extension = platform_native_extension();
    const String waiter_relative_path = "bin/" + native_platform + "/" + platform_update_waiter_filename();
    for (int i = 0; i < entries.size(); i++) {
        const String archive_path = entries[i];
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
        } else if (relative == waiter_relative_path) {
            has_waiter = true;
        } else if (!native_extension.is_empty() && relative.begins_with("bin/" + native_platform + "/") && relative.get_extension().to_lower() == native_extension) {
            has_native = true;
        }
    }
    zip->close();

    Ref<ConfigFile> config;
    config.instantiate();
    if (config->parse(plugin_config_text) != OK || String(config->get_value("plugin", "version", "")) != version ||
            !has_descriptor || !has_helper || !has_native || !has_waiter) {
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
    const String waiter_source = staged_addon.path_join(waiter_relative_path);
    const String waiter_path = helper_root.path_join(platform_update_waiter_filename());
    if (!write_bytes(helper_script, helper_data) ||
            DirAccess::copy_absolute(waiter_source, waiter_path) != OK ||
            !write_text(helper_root.path_join("project.godot"), "[application]\nconfig/name=\"Godot Minigame Updater\"\n")) {
        r_error = "Cannot create update helper project.";
        return false;
    }
    if (native_platform != "windows" && FileAccess::set_unix_permissions(waiter_path, 0755) != OK) {
        r_error = "Cannot make the update waiter executable.";
        return false;
    }

    const String install_root = get_update_cache_root().path_join("install").path_join(version);
    remove_tree(install_root);
    DirAccess::make_dir_recursive_absolute(install_root);

    Dictionary manifest;
    manifest["project_path"] = ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
    manifest["addon_path"] = ProjectSettings::get_singleton()->globalize_path("res://addons/godot-minigame").trim_suffix("/");
    manifest["staged_addon_path"] = staged_addon;
    manifest["install_root"] = install_root;
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
    const String ready_path = helper_root.path_join("waiter.ready");
    DirAccess::remove_absolute(ready_path);
    PackedStringArray waiter_args;
    waiter_args.append(String::num_int64(OS::get_singleton()->get_process_id()));
    waiter_args.append(ready_path);
    waiter_args.append(OS::get_singleton()->get_executable_path());
    waiter_args.append(helper_root);
    waiter_args.append(helper_script);
    waiter_args.append(manifest_path);
    waiter_args.append(String(manifest["result_path"]));
    waiter_args.append(version);
    const int64_t waiter_pid = OS::get_singleton()->create_process(waiter_path, waiter_args, false);
    if (waiter_pid <= 0) {
        r_error = "Update waiter process could not be started.";
        return false;
    }
    bool waiter_ready = false;
    for (int attempt = 0; attempt < 100; attempt++) {
        if (FileAccess::file_exists(ready_path)) {
            waiter_ready = true;
            break;
        }
        if (!OS::get_singleton()->is_process_running(waiter_pid)) {
            break;
        }
        OS::get_singleton()->delay_usec(50000);
    }
    if (!waiter_ready) {
        OS::get_singleton()->kill(waiter_pid);
        r_error = "Update waiter could not attach to the editor process.";
        return false;
    }
    DirAccess::remove_absolute(ready_path);
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

void UpdateManager::clear_pending_update() {
    if (current_state == STATE_INSTALLING) {
        return;
    }
    if (current_state == STATE_CHECKING && version_checker) {
        version_checker->cancel_request();
    }
    if (current_state == STATE_DOWNLOADING && downloader) {
        downloader->cancel_request();
    }
    const String cache_root = get_update_cache_root().simplify_path().trim_suffix("/");
    const String cached_path = download_file_path.simplify_path();
    if (!cached_path.is_empty() && cached_path.begins_with(cache_root + String("/")) && FileAccess::file_exists(cached_path)) {
        DirAccess::remove_absolute(cached_path);
    }
    remove_tree(cache_root.path_join("local"));
    DirAccess::remove_absolute(cache_root.path_join("local-package.incoming.zip"));
    download_file_path = String();
    expected_download_sha256 = String();
    remote_version_info.clear();
    set_state(STATE_IDLE);
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
