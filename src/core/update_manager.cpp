#include "core/update_manager.h"
#include "core/toolkit_core.h"
#include "core/logging.h"
#include "core/types.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/timer.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <vector>

namespace {

struct PluginInstallEntry {
    godot::String relative_path;
    godot::String destination_path;
    godot::String temporary_path;
    godot::PackedByteArray data;
    godot::PackedByteArray previous_data;
    bool had_previous = false;
    bool committed = false;
};

bool read_file_bytes(const godot::String &p_path, godot::PackedByteArray &r_data) {
    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_path, godot::FileAccess::READ);
    if (file.is_null()) {
        return false;
    }
    const int64_t length = file->get_length();
    r_data = file->get_buffer(length);
    return r_data.size() == length;
}

bool write_file_bytes(const godot::String &p_path, const godot::PackedByteArray &p_data) {
    godot::Error dir_error = godot::DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
    if (dir_error != godot::OK && dir_error != godot::ERR_ALREADY_EXISTS) {
        return false;
    }
    godot::Ref<godot::FileAccess> file = godot::FileAccess::open(p_path, godot::FileAccess::WRITE);
    if (file.is_null()) {
        return false;
    }
    file->store_buffer(p_data);
    file->flush();
    return file->get_error() == godot::OK;
}

bool is_safe_package_path(const godot::String &p_relative_path) {
    if (p_relative_path.is_empty() || p_relative_path.is_absolute_path() || p_relative_path.contains(":")) {
        return false;
    }
    if (p_relative_path.simplify_path() != p_relative_path) {
        return false;
    }
    godot::PackedStringArray components = p_relative_path.split("/", false);
    for (int i = 0; i < components.size(); i++) {
        if (components[i] == ".." || components[i] == ".") {
            return false;
        }
    }
    return true;
}

godot::String expected_native_relative_path(const godot::String &p_version) {
    godot::String platform = godot::OS::get_singleton()->get_name().to_lower();
    if (platform == "windows") {
        return "bin/windows/godot-minigame.windows.x86_64." + p_version + ".dll";
    }
    if (platform == "linux") {
        return "bin/linux/libgodot-minigame.linux.x86_64." + p_version + ".so";
    }
    if (platform == "macos") {
        return "bin/macos/libgodot-minigame.macos." + p_version + ".dylib";
    }
    return "";
}

godot::String current_platform_library_key() {
    godot::String platform = godot::OS::get_singleton()->get_name().to_lower();
    if (platform == "windows") {
        return "windows.x86_64";
    }
    if (platform == "linux") {
        return "linux.x86_64";
    }
    if (platform == "macos") {
        return "macos";
    }
    return "";
}

int install_priority(const godot::String &p_relative_path) {
    if (p_relative_path == "godot-minigame.gdextension") {
        return 1;
    }
    if (p_relative_path == "plugin.cfg") {
        return 2;
    }
    return 0;
}

} // namespace

namespace toolkit {

UpdateManager *UpdateManager::singleton = nullptr;

UpdateManager *UpdateManager::get_singleton() {
    return singleton;
}

UpdateManager::UpdateManager() {
    if (singleton == nullptr) {
        singleton = this;
    }

    // Don't create HTTP nodes in constructor
    // They will be created later in initialize()
    version_checker = nullptr;
    downloader = nullptr;
    http_client = nullptr;
    polling_timer = nullptr;

    // Initialize tracking array
    active_http_nodes = Array();

    // Initialize progress tracking variables
    download_total_bytes = 0;
    download_received_bytes = 0;
    is_downloading_with_progress = false;
}

UpdateManager::~UpdateManager() {
    // CRITICAL FIX: Actively clean up HTTPRequest and Timer nodes before engine shutdown
    // This prevents potential crashes during editor termination
    cleanup_http_nodes();

    // The http_client, however, is not a Node and was manually allocated, so it should be deleted.
    if (http_client) {
        memdelete(http_client);
        http_client = nullptr;
    }

    if (singleton == this) {
        singleton = nullptr;
    }
}

void UpdateManager::_bind_methods() {
    using namespace godot;

    ClassDB::bind_method(D_METHOD("initialize"), &UpdateManager::initialize);
    ClassDB::bind_method(D_METHOD("check_for_updates", "local_version"), &UpdateManager::check_for_updates);
    ClassDB::bind_method(D_METHOD("download_update"), &UpdateManager::download_update);
    ClassDB::bind_method(D_METHOD("cancel_download"), &UpdateManager::cancel_download);
    ClassDB::bind_method(D_METHOD("get_local_version"), &UpdateManager::get_local_version);
    ClassDB::bind_method(D_METHOD("get_remote_version_info"), &UpdateManager::get_remote_version_info);
    ClassDB::bind_method(D_METHOD("get_current_state"), &UpdateManager::get_current_state);
    ClassDB::bind_method(D_METHOD("get_download_file_path"), &UpdateManager::get_download_file_path);
    ClassDB::bind_method(D_METHOD("install_downloaded_update"), &UpdateManager::install_downloaded_update);
   
    ClassDB::bind_method(D_METHOD("_on_version_check_completed"), &UpdateManager::_on_version_check_completed);
    ClassDB::bind_method(D_METHOD("_on_download_completed"), &UpdateManager::_on_download_completed);
    ClassDB::bind_method(D_METHOD("update_polling"), &UpdateManager::update_polling);
   
    ADD_SIGNAL(MethodInfo("update_available", PropertyInfo(Variant::DICTIONARY, "version_info")));
    ADD_SIGNAL(MethodInfo("download_finished", PropertyInfo(Variant::BOOL, "success")));
    ADD_SIGNAL(MethodInfo("installation_finished", PropertyInfo(Variant::BOOL, "success"), PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("download_progress_changed", PropertyInfo(Variant::INT, "bytes_received"), PropertyInfo(Variant::INT, "total_bytes")));
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
    using namespace godot;

    TOOLKIT_LOG("UpdateManager: Starting initialization...");

    cleanup_stale_native_libraries();

    // Check if already initialized
    if (version_checker && version_checker->get_parent()) {
        TOOLKIT_LOG("UpdateManager: Already initialized, skipping");
        return;
    }

    // Create HTTP nodes if they don't exist
    if (!version_checker) {
        version_checker = memnew(HTTPRequest);
        version_checker->set_name("UpdateManager_VersionChecker");
    }
    if (!downloader) {
        downloader = memnew(HTTPRequest);
        downloader->set_name("UpdateManager_Downloader");
    }
    if (!http_client) {
        http_client = memnew(HTTPClient);
    }
    if (!polling_timer) {
        polling_timer = memnew(Timer);
        polling_timer->set_name("UpdateManager_PollingTimer");
        polling_timer->set_wait_time(0.1); // Poll 10 times per second
        polling_timer->set_autostart(false);
    }

    // Get a safe parent node for HTTPRequest nodes
    Node* parent_node = nullptr;

    // Try to get editor interface only when running in editor context.
    if (Engine::get_singleton()->is_editor_hint()) {
        EditorInterface* editor = EditorInterface::get_singleton();
        if (editor) {
            Node* main_screen = editor->get_editor_main_screen();
            if (main_screen) {
                parent_node = main_screen;
                TOOLKIT_LOG("UpdateManager: Using editor main screen as parent");
            }
        }
    }

    // If editor interface failed, try engine singleton
    if (!parent_node && Engine::get_singleton()) {
        // Try to find the main loop
        MainLoop* main_loop = Engine::get_singleton()->get_main_loop();
        Node* main_loop_node = Object::cast_to<Node>(main_loop);
        if (main_loop_node) {
            parent_node = main_loop_node;
            TOOLKIT_LOG("UpdateManager: Using main loop as parent");
        }
    }

    if (parent_node) {
        // Add nodes to scene tree and connect signals
        if (!version_checker->get_parent()) {
            parent_node->add_child(version_checker);
            version_checker->connect("request_completed", callable_mp(this, &UpdateManager::_on_version_check_completed));
            track_http_node(version_checker);
        }

        if (!downloader->get_parent()) {
            parent_node->add_child(downloader);
            downloader->connect("request_completed", callable_mp(this, &UpdateManager::_on_download_completed));
            track_http_node(downloader);
        }

        if (!polling_timer->get_parent()) {
            parent_node->add_child(polling_timer);
            polling_timer->connect("timeout", callable_mp(this, &UpdateManager::update_polling));
            track_http_node(polling_timer);
        }

        TOOLKIT_LOG("UpdateManager: HTTPRequest nodes and Timer initialized successfully");
    } else {
        TOOLKIT_LOG("UpdateManager: No suitable parent found, deferring initialization");
        // Defer initialization and try again
        call_deferred("initialize");
    }
   }
   
   void UpdateManager::check_for_updates(const godot::String &p_local_version) {
    using namespace godot;

    if (current_state == STATE_CHECKING || current_state == STATE_DOWNLOADING || current_state == STATE_INSTALLING) {
    	TOOLKIT_LOG("UpdateManager: Already checking/downloading, skipping");
    	return;
    }

    if (!version_checker || !version_checker->get_parent()) {
    	TOOLKIT_LOG("UpdateManager: Not properly initialized, attempting initialization");
    	initialize();
    	if (!version_checker || !version_checker->get_parent()) {
    		set_state(STATE_ERROR);
    		emit_signal("error", "UpdateManager initialization failed.");
    		return;
    	}
    }

    if (!is_properly_configured()) {
		set_state(STATE_ERROR);
		emit_signal("error", "Plugin update manifest URL is not configured.");
		return;
    }

    local_version = p_local_version;
    String update_url = resolve_update_manifest_url();

    TOOLKIT_LOG("UpdateManager: Checking for updates from: ", update_url);
    TOOLKIT_LOG("UpdateManager: Current version: ", local_version);

    // Configure HTTPRequest for better reliability
    version_checker->set_timeout(30.0); // 30 second timeout
    version_checker->set_download_chunk_size(4096);

    set_state(STATE_CHECKING);
    Error err = version_checker->request(update_url);

    if (err != OK) {
    	set_state(STATE_ERROR);
    	String error_msg = "Failed to start version check request. Error code: " + String::num_int64(err);
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    } else {
    	TOOLKIT_LOG("UpdateManager: Version check request started successfully");
    }
   }

void UpdateManager::_on_version_check_completed(int p_result, int p_response_code, const godot::PackedStringArray &p_headers, const godot::PackedByteArray &p_body) {
    using namespace godot;

    TOOLKIT_LOG("UpdateManager: Version check completed. Result: ", p_result, ", Response code: ", p_response_code);

    // Check for HTTP errors
    if (p_result != HTTPRequest::RESULT_SUCCESS) {
    	set_state(STATE_ERROR);
    	String error_msg = "Version check failed. HTTP result: " + String::num(p_result);
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    	return;
    }

    if (p_response_code != 200) {
    	set_state(STATE_ERROR);
    	String error_msg = "Version check failed with HTTP status: " + String::num(p_response_code);
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    	return;
    }

    // Parse response body
    String response_text = p_body.get_string_from_utf8();
    TOOLKIT_LOG("UpdateManager: Received response: ", response_text.substr(0, 200), "...");

    Ref<JSON> json = memnew(JSON);
    Error err = json->parse(response_text);
    if (err != OK) {
    	set_state(STATE_ERROR);
    	String error_msg = "Failed to parse remote version JSON. Parse error: " + String::num(err);
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    	return;
    }

    Variant result = json->get_data();
    if (result.get_type() != Variant::DICTIONARY) {
    	set_state(STATE_ERROR);
    	emit_signal("error", "Remote version data is not a dictionary.");
    	return;
    }

    remote_version_info = result;
    bool published = bool(remote_version_info.get("published", true));
    String remote_v_str = String(remote_version_info.get("version", "")).strip_edges();
    VersionInfo local = VersionInfo::from_string(local_version);
    VersionInfo remote = VersionInfo::from_string(remote_v_str);

    TOOLKIT_LOG("UpdateManager: Remote version: ", remote_v_str, ", Local version: ", local_version);

    if (published && !remote_v_str.is_empty() && remote.is_newer_than(local)) {
		set_state(STATE_UPDATE_AVAILABLE);
		TOOLKIT_LOG("UpdateManager: Plugin update available.");
		emit_signal("update_available", remote_version_info);
    } else {
		set_state(STATE_UP_TO_DATE);
		TOOLKIT_LOG("UpdateManager: No published plugin update is newer than the local version.");
    }
   }

void UpdateManager::download_update() {
    using namespace godot;

    if (current_state != STATE_UPDATE_AVAILABLE) {
    	TOOLKIT_LOG("UpdateManager: Download called but no update available. Current state: ", current_state);
    	return;
    }

    if (!downloader || !downloader->get_parent()) {
    	TOOLKIT_LOG("UpdateManager: Downloader not initialized, attempting initialization");
    	initialize();
    	if (!downloader || !downloader->get_parent()) {
    		set_state(STATE_ERROR);
    		emit_signal("error", "UpdateManager initialization failed for download.");
    		return;
    	}
    }

    String platform = OS::get_singleton()->get_name().to_lower();
    String arch = Engine::get_singleton()->get_architecture_name();
    String platform_key;

    if (platform == "macos") {
        platform_key = "macos-universal";
    } else {
        platform_key = platform + "-" + arch;
    }

    TOOLKIT_LOG("UpdateManager: Looking for platform key: ", platform_key);

    Dictionary platforms = remote_version_info.get("platforms", Dictionary());
    if (!platforms.has(platform_key)) {
    	set_state(STATE_ERROR);
    	String error_msg = "No update package found for platform: " + platform_key;
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    	return;
    }

    Dictionary platform_data = platforms[platform_key];
    String asset_name = resolve_platform_asset_name(platform_data);
    if (asset_name.is_empty()) {
    	set_state(STATE_ERROR);
    	String error_msg = "Update asset is missing for platform: " + platform_key;
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    	return;
    }

    String full_url = resolve_update_asset_url(platform_data);
    if (full_url.is_empty()) {
        set_state(STATE_ERROR);
        String error_msg = "Update asset URL is missing for platform: " + platform_key;
        TOOLKIT_LOG("UpdateManager: ", error_msg);
        emit_signal("error", error_msg);
        return;
    }
    expected_download_sha256 = String(platform_data.get("sha256", "")).strip_edges().to_lower();
    if (expected_download_sha256.length() != 64) {
        set_state(STATE_ERROR);
        emit_signal("error", "Update asset is missing a valid SHA-256 digest.");
        return;
    }
    String download_path = "user://godot-minigame/updates/" + asset_name;
    DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(download_path.get_base_dir()));
    download_file_path = download_path;

    TOOLKIT_LOG("UpdateManager: Starting download from: ", full_url);
    TOOLKIT_LOG("UpdateManager: Download destination: ", download_path);

    set_state(STATE_DOWNLOADING);

    // Configure HTTPRequest for download
    downloader->set_download_file(download_path);
    downloader->set_timeout(60.0); // 60-second timeout for downloads
    downloader->set_download_chunk_size(16384); // 16KB chunk size

    Error err = downloader->request(full_url);

    if (err != OK) {
        set_state(STATE_ERROR);
        String error_msg = "Failed to start download request. Error code: " + String::num_int64(err);
        TOOLKIT_LOG("UpdateManager: ", error_msg);
        emit_signal("error", error_msg);
    } else {
        TOOLKIT_LOG("UpdateManager: Download request started successfully");
    }
}

void UpdateManager::_on_download_completed(int p_result, int p_response_code, const godot::PackedStringArray &p_headers, const godot::PackedByteArray &p_body) {
    using namespace godot;

    TOOLKIT_LOG("UpdateManager: Download completed. Result: ", p_result, ", Response code: ", p_response_code);

    if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code == 200) {
		String download_path = download_file_path;
    	if (FileAccess::file_exists(download_path)) {
            String actual_sha256 = FileAccess::get_sha256(download_path).to_lower();
            if (actual_sha256 != expected_download_sha256) {
                set_state(STATE_ERROR);
                emit_signal("download_finished", false);
                emit_signal("error", "Downloaded plugin package failed SHA-256 verification.");
                DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(download_path));
                return;
            }

    		Ref<FileAccess> file = FileAccess::open(download_path, FileAccess::READ);
    		if (file.is_valid()) {
    			int64_t file_size = file->get_length();
    			file->close();
    			TOOLKIT_LOG("UpdateManager: Download successful. File size: ", file_size, " bytes");
    			set_state(STATE_DOWNLOADED);
    			emit_signal("download_finished", true);
                call_deferred("install_downloaded_update");
    		} else {
    			set_state(STATE_ERROR);
    			emit_signal("download_finished", false);
    			emit_signal("error", "Downloaded file could not be opened.");
    		}
    	} else {
    		set_state(STATE_ERROR);
    		emit_signal("download_finished", false);
    		emit_signal("error", "Downloaded file not found.");
    	}
    } else {
    	set_state(STATE_ERROR);
    	emit_signal("download_finished", false);
    	String error_msg = "Download failed. HTTP result: " + String::num(p_result) + ", Status code: " + String::num(p_response_code);
    	TOOLKIT_LOG("UpdateManager: ", error_msg);
    	emit_signal("error", error_msg);
    }
   }

bool UpdateManager::is_properly_configured() const {
    return !resolve_update_manifest_url().is_empty();
}

String UpdateManager::get_download_file_path() const {
    return download_file_path;
}

String UpdateManager::resolve_update_manifest_url() const {
    String environment_url = OS::get_singleton()->get_environment("GODOT_MINIGAME_PLUGIN_UPDATE_URL").strip_edges();
    if (!environment_url.is_empty()) {
        return environment_url;
    }

    const String setting_name = "godot_minigame/plugin_update_manifest_url";
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings && settings->has_setting(setting_name)) {
        String configured_url = String(settings->get_setting(setting_name, "")).strip_edges();
        if (!configured_url.is_empty()) {
            return configured_url;
        }
    }

    return "https://raw.githubusercontent.com/Losomz/godot-minigame/main/catalog/plugin-stable.json";
}

String UpdateManager::resolve_update_asset_url(const Dictionary &platform_data) const {
    String direct_url = String(platform_data.get("url", "")).strip_edges();
    if (direct_url.begins_with("https://")) {
        return direct_url;
    }
    return "";
}

String UpdateManager::resolve_platform_asset_name(const Dictionary &platform_data) const {
    String asset = platform_data.get("asset", "");
    if (!asset.strip_edges().is_empty()) {
        return asset.get_file();
    }

    // Backward compatibility for older latest.json format.
    String legacy_url = platform_data.get("url", "");
    if (legacy_url.strip_edges().is_empty()) {
        return "";
    }
    return legacy_url.get_file();
}

} // namespace toolkit

// _on_download_progress method removed - HTTPRequest doesn't support progress tracking in Godot 4.x

void toolkit::UpdateManager::set_state(UpdateState p_state) {
	if (current_state != p_state) {
		current_state = p_state;
		emit_signal("update_state_changed", current_state);
	}
}

void toolkit::UpdateManager::cancel_download() {
	if (current_state == STATE_DOWNLOADING && downloader) {
		downloader->cancel_request();
		set_state(STATE_UPDATE_AVAILABLE); // Revert to previous state
	}
}

bool toolkit::UpdateManager::perform_update(godot::String &r_error) {
	using namespace godot;

	const String update_package = download_file_path;
	if (update_package.is_empty()) {
		r_error = "No downloaded plugin update is available.";
		return false;
	}
	if (!FileAccess::file_exists(update_package)) {
		r_error = "Downloaded update package was not found.";
		return false;
	}
	String remote_version = String(remote_version_info.get("version", "")).strip_edges();
	if (remote_version.is_empty() || remote_version.contains("/") || remote_version.contains("\\") || remote_version.contains(":")) {
		r_error = "Update manifest contains an invalid plugin version.";
		return false;
	}
	String required_native_path = expected_native_relative_path(remote_version);
	if (required_native_path.is_empty()) {
		r_error = "Automatic installation is not supported on this platform.";
		return false;
	}

	TOOLKIT_LOG("--- Starting C++ update process ---");

	Ref<ZIPReader> zip_reader = memnew(ZIPReader);
	Error zip_err = zip_reader->open(update_package);
	if (zip_err != OK) {
		r_error = "Failed to open downloaded ZIP package.";
		return false;
	}

	PackedStringArray files = zip_reader->get_files();
	String addon_path = ProjectSettings::get_singleton()->globalize_path("res://addons/godot-minigame/");
	const String package_prefix = "addons/godot-minigame/";
	HashSet<String> package_paths;
	std::vector<PluginInstallEntry> entries;
	String plugin_config_content;
	String extension_content;
	bool has_required_native = false;

	for (int i = 0; i < files.size(); i++) {
		String archive_path = String(files[i]).replace("\\", "/");
		if (!archive_path.begins_with(package_prefix)) {
			r_error = "Update package contains a file outside addons/godot-minigame: " + archive_path;
			zip_reader->close();
			return false;
		}

		String relative_path = archive_path.substr(package_prefix.length());
		if (relative_path.is_empty() || archive_path.ends_with("/")) {
			continue;
		}
		if (!is_safe_package_path(relative_path)) {
			r_error = "Update package contains an unsafe path: " + archive_path;
			zip_reader->close();
			return false;
		}
		String package_path_key = relative_path.to_lower();
		if (package_paths.has(package_path_key)) {
			r_error = "Update package contains a duplicate path: " + relative_path;
			zip_reader->close();
			return false;
		}
		package_paths.insert(package_path_key);

		PluginInstallEntry entry;
		entry.relative_path = relative_path;
		entry.destination_path = addon_path.path_join(relative_path);
		entry.temporary_path = entry.destination_path + ".godot-minigame-update";
		entry.data = zip_reader->read_file(files[i]);
		if (relative_path == "plugin.cfg") {
			plugin_config_content = entry.data.get_string_from_utf8();
		} else if (relative_path == "godot-minigame.gdextension") {
			extension_content = entry.data.get_string_from_utf8();
		} else if (relative_path == required_native_path) {
			has_required_native = true;
		}
		entries.push_back(entry);
	}
	zip_reader->close();

	Ref<ConfigFile> package_plugin_config;
	package_plugin_config.instantiate();
	if (!package_paths.has("plugin.cfg") || package_plugin_config->parse(plugin_config_content) != OK ||
			String(package_plugin_config->get_value("plugin", "version", "")).strip_edges() != remote_version) {
		r_error = "Update package plugin.cfg does not match version " + remote_version + ".";
		return false;
	}
	Ref<ConfigFile> package_extension_config;
	package_extension_config.instantiate();
	String library_key = current_platform_library_key();
	String configured_library;
	if (package_paths.has("godot-minigame.gdextension") && package_extension_config->parse(extension_content) == OK) {
		configured_library = String(package_extension_config->get_value("libraries", library_key, "")).strip_edges().trim_prefix("./");
	}
	if (library_key.is_empty() || configured_library != required_native_path) {
		r_error = "Update package GDExtension descriptor does not reference the versioned native library.";
		return false;
	}
	if (!has_required_native) {
		r_error = "Update package is missing the native library for this platform: " + required_native_path;
		return false;
	}

	std::stable_sort(entries.begin(), entries.end(), [](const PluginInstallEntry &a, const PluginInstallEntry &b) {
		return install_priority(a.relative_path) < install_priority(b.relative_path);
	});

	for (PluginInstallEntry &entry : entries) {
		entry.had_previous = FileAccess::file_exists(entry.destination_path);
		if (entry.had_previous && !read_file_bytes(entry.destination_path, entry.previous_data)) {
			r_error = "Failed to back up existing plugin file: " + entry.relative_path;
			for (PluginInstallEntry &cleanup_entry : entries) {
				DirAccess::remove_absolute(cleanup_entry.temporary_path);
			}
			return false;
		}
		if (!write_file_bytes(entry.temporary_path, entry.data)) {
			r_error = "Failed to stage plugin file: " + entry.relative_path;
			for (PluginInstallEntry &cleanup_entry : entries) {
				DirAccess::remove_absolute(cleanup_entry.temporary_path);
			}
			return false;
		}
	}

	auto rollback = [&entries]() -> bool {
		bool rollback_ok = true;
		for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
			PluginInstallEntry &entry = *it;
			DirAccess::remove_absolute(entry.temporary_path);
			if (!entry.committed) {
				continue;
			}
			DirAccess::remove_absolute(entry.destination_path);
			if (entry.had_previous && !write_file_bytes(entry.destination_path, entry.previous_data)) {
				rollback_ok = false;
			}
		}
		return rollback_ok;
	};

	for (PluginInstallEntry &entry : entries) {
		if (FileAccess::file_exists(entry.destination_path)) {
			Error remove_error = DirAccess::remove_absolute(entry.destination_path);
			if (remove_error != OK) {
				r_error = "Failed to replace plugin file: " + entry.relative_path;
				if (!rollback()) {
					r_error += " Rollback was incomplete.";
				}
				return false;
			}
		}
		Error rename_error = DirAccess::rename_absolute(entry.temporary_path, entry.destination_path);
		if (rename_error != OK) {
			r_error = "Failed to commit plugin file: " + entry.relative_path;
			if (entry.had_previous && !write_file_bytes(entry.destination_path, entry.previous_data)) {
				r_error += " The current file could not be restored.";
			}
			if (!rollback()) {
				r_error += " Rollback was incomplete.";
			}
			return false;
		}
		entry.committed = true;
	}

	String absolute_package_path = ProjectSettings::get_singleton()->globalize_path(update_package);
	Error remove_package_error = DirAccess::remove_absolute(absolute_package_path);
	if (remove_package_error != OK) {
		TOOLKIT_LOG("UpdateManager: Installed update but could not remove package: ", absolute_package_path);
	}
	local_version = remote_version;
	TOOLKIT_LOG("--- C++ update process finished ---");
	return true;
}

void toolkit::UpdateManager::install_downloaded_update() {
	using namespace godot;
	if (current_state != STATE_DOWNLOADED) {
		return;
	}
	if (!Engine::get_singleton()->is_editor_hint() || !EditorInterface::get_singleton()) {
		set_state(STATE_ERROR);
		String error_message = "Godot editor restart interface is unavailable; update was not installed.";
		emit_signal("error", error_message);
		emit_signal("installation_finished", false, error_message);
		return;
	}

	set_state(STATE_INSTALLING);
	String error_message;
	if (!perform_update(error_message)) {
		set_state(STATE_ERROR);
		emit_signal("error", error_message);
		emit_signal("installation_finished", false, error_message);
		return;
	}

	emit_signal("installation_finished", true, "");
	TOOLKIT_LOG("Update applied. Restarting editor...");
	EditorInterface::get_singleton()->restart_editor(true);
}

void toolkit::UpdateManager::restart_editor_for_update() {
	install_downloaded_update();
}

void toolkit::UpdateManager::cleanup_stale_native_libraries() {
	using namespace godot;
	if (stale_native_cleanup_attempted) {
		return;
	}
	stale_native_cleanup_attempted = true;

	PackedByteArray descriptor_data;
	String descriptor_path = ProjectSettings::get_singleton()->globalize_path("res://addons/godot-minigame/godot-minigame.gdextension");
	if (!read_file_bytes(descriptor_path, descriptor_data)) {
		return;
	}
	String descriptor = descriptor_data.get_string_from_utf8();
	const char *platform_dirs[] = { "windows", "linux", "macos" };
	for (const char *platform_dir : platform_dirs) {
		String directory_path = ProjectSettings::get_singleton()->globalize_path(
			String("res://addons/godot-minigame/bin/") + platform_dir);
		Ref<DirAccess> directory = DirAccess::open(directory_path);
		if (directory.is_null()) {
			continue;
		}
		directory->list_dir_begin();
		String file_name = directory->get_next();
		while (!file_name.is_empty()) {
			bool is_directory = directory->current_is_dir();
			bool known_library =
				(file_name.begins_with("godot-minigame.windows.") && file_name.ends_with(".dll")) ||
				(file_name.begins_with("libgodot-minigame.linux.") && file_name.ends_with(".so")) ||
				(file_name.begins_with("libgodot-minigame.macos.") && file_name.ends_with(".dylib"));
			if (!is_directory && known_library && !descriptor.contains(file_name)) {
				String stale_path = directory_path.path_join(file_name);
				Error remove_error = DirAccess::remove_absolute(stale_path);
				if (remove_error == OK) {
					TOOLKIT_LOG("UpdateManager: Removed stale native library: ", stale_path);
				} else {
					TOOLKIT_LOG("UpdateManager: Could not remove stale native library: ", stale_path);
				}
			}
			file_name = directory->get_next();
		}
		directory->list_dir_end();
	}
}

toolkit::UpdateManager::UpdateState toolkit::UpdateManager::get_current_state() const {
	return current_state;
}

godot::String toolkit::UpdateManager::get_local_version() const {
	return local_version;
}

godot::Dictionary toolkit::UpdateManager::get_remote_version_info() const {
	return remote_version_info;
}

// The HTTPClient-based download methods are no longer needed and can be removed.
// The update_polling method is also no longer needed for downloads.
void toolkit::UpdateManager::update_polling() {
    // This method can be kept for future polling needs, but is not used for downloads anymore.
}

// HTTP nodes lifecycle management implementation
void toolkit::UpdateManager::track_http_node(godot::Node* node) {
    if (node && !active_http_nodes.has(node)) {
        active_http_nodes.append(node);
    }
}

void toolkit::UpdateManager::cleanup_http_nodes() {
    using namespace godot;

    TOOLKIT_LOG("UpdateManager: Cleaning up ", active_http_nodes.size(), " HTTP nodes");

    for (int i = 0; i < active_http_nodes.size(); i++) {
        Object* obj = active_http_nodes[i];
        Node* node = Object::cast_to<Node>(obj);

        if (node && node->get_parent()) {
            TOOLKIT_LOG("UpdateManager: Removing HTTP node: ", node->get_name());

            // Disconnect all signals to prevent callbacks during destruction
            if (node->has_method("disconnect_all_signals")) {
                node->call("disconnect_all_signals");
            }

            // Remove from parent and queue for deletion
            node->get_parent()->remove_child(node);
            node->queue_free();
        }
    }

    active_http_nodes.clear();

    // Clear direct references
    version_checker = nullptr;
    downloader = nullptr;
    polling_timer = nullptr;

    TOOLKIT_LOG("UpdateManager: HTTP nodes cleanup completed");
}
