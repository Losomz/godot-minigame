#include "templates/template_manager.h"
#include "network/download_manager.h"
#include "filesystem/user_data_path.h"
#include "core/logging.h"
#include "core/network_proxy.h"
#include "core/plugin_state_store.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/editor_settings.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "core/plugin_version.h"

#ifdef EMBED_RESOURCES
#include "resources/embedded_resources.h"
#endif

using namespace godot;
// Reduce namespace verbosity
using namespace toolkit::filesystem;

namespace toolkit {
namespace templates {

static bool _parse_http_url(const String &url, String &host, int &port, String &path, bool &use_tls);

namespace {
constexpr const char *EDITOR_SETTING_PREFIX = "godot_minigame/templates/";
constexpr const char *TEMPLATE_STATE_SECTION = "templates";
constexpr const char *DEFAULT_TEMPLATE_CATALOG_URL = "https://raw.githubusercontent.com/Losomz/godot-minigame/main/plugin/catalog/templates.json";

bool _is_redirect_response_code(int response_code) {
    return response_code == HTTPClient::RESPONSE_MOVED_PERMANENTLY ||
            response_code == HTTPClient::RESPONSE_FOUND ||
            response_code == HTTPClient::RESPONSE_SEE_OTHER ||
            response_code == HTTPClient::RESPONSE_TEMPORARY_REDIRECT ||
            response_code == HTTPClient::RESPONSE_PERMANENT_REDIRECT;
}

String _get_header_value_case_insensitive(const Dictionary &headers, const String &header_name) {
    Array keys = headers.keys();
    String normalized_header_name = header_name.to_lower();
    for (int i = 0; i < keys.size(); i++) {
        String key = String(keys[i]);
        if (key.to_lower() == normalized_header_name) {
            return String(headers[key]).strip_edges();
        }
    }
    return "";
}

String _build_absolute_redirect_url(const String &base_url, const String &location) {
    String normalized_location = location.strip_edges();
    if (normalized_location.is_empty()) {
        return "";
    }
    if (normalized_location.begins_with("http://") || normalized_location.begins_with("https://")) {
        return normalized_location;
    }

    String host;
    String path;
    int port = 0;
    bool use_tls = false;
    if (!_parse_http_url(base_url, host, port, path, use_tls)) {
        return normalized_location;
    }

    String scheme = use_tls ? "https://" : "http://";
    String host_with_port = host;
    bool use_default_port = (use_tls && port == 443) || (!use_tls && port == 80);
    if (!use_default_port) {
        host_with_port += ":" + String::num_int64(port);
    }

    if (normalized_location.begins_with("/")) {
        return scheme + host_with_port + normalized_location;
    }

    String base_path = path.get_base_dir();
    if (base_path.is_empty() || base_path == ".") {
        base_path = "/";
    }
    return scheme + host_with_port + base_path.path_join(normalized_location);
}

Error _remove_directory_recursive_absolute(const String &dir_path) {
    if (!DirAccess::dir_exists_absolute(dir_path)) {
        return OK;
    }

    Ref<DirAccess> dir = DirAccess::open(dir_path);
    if (dir.is_null()) {
        return ERR_CANT_OPEN;
    }

    dir->list_dir_begin();
    while (true) {
        String entry = dir->get_next();
        if (entry.is_empty()) {
            break;
        }
        if (entry == "." || entry == "..") {
            continue;
        }

        String child_path = dir_path.path_join(entry);
        Error child_err = OK;
        if (dir->current_is_dir()) {
            child_err = _remove_directory_recursive_absolute(child_path);
        } else {
            child_err = DirAccess::remove_absolute(child_path);
        }

        if (child_err != OK) {
            dir->list_dir_end();
            return child_err;
        }
    }
    dir->list_dir_end();

    return DirAccess::remove_absolute(dir_path);
}

bool _is_sha256(const String &value) {
    if (value.length() != 64) {
        return false;
    }
    String normalized = value.to_lower();
    for (int i = 0; i < normalized.length(); i++) {
        char32_t c = normalized[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool _is_semver(const String &value) {
    String core = value.get_slice("-", 0);
    PackedStringArray parts = core.split(".");
    if (parts.size() != 3) {
        return false;
    }
    for (int i = 0; i < parts.size(); i++) {
        String part = String(parts[i]);
        if (part.is_empty() || !part.is_valid_int() || part.to_int() < 0) {
            return false;
        }
    }
    return true;
}
}

TemplateManager* TemplateManager::singleton = nullptr;

static bool _parse_http_url(const String &url, String &host, int &port, String &path, bool &use_tls) {
    String rest;
    if (url.begins_with("https://")) {
        use_tls = true;
        port = 443;
        rest = url.substr(8);
    } else if (url.begins_with("http://")) {
        use_tls = false;
        port = 80;
        rest = url.substr(7);
    } else {
        return false;
    }

    int slash_pos = rest.find("/");
    String host_port = slash_pos >= 0 ? rest.substr(0, slash_pos) : rest;
    path = slash_pos >= 0 ? rest.substr(slash_pos) : "/";
    if (path.is_empty()) {
        path = "/";
    }

    int colon_pos = host_port.rfind(":");
    if (colon_pos > 0) {
        String parsed_host = host_port.substr(0, colon_pos);
        String parsed_port = host_port.substr(colon_pos + 1);
        if (!parsed_port.is_empty()) {
            int custom_port = parsed_port.to_int();
            if (custom_port > 0) {
                port = custom_port;
                host = parsed_host;
                return !host.is_empty();
            }
        }
    }

    host = host_port;
    return !host.is_empty();
}

static String _resolve_absolute_output_path(const String &output_path) {
    if (output_path.is_empty()) {
        return ProjectSettings::get_singleton()->globalize_path("res://");
    }
    if (output_path.is_absolute_path()) {
        return output_path;
    }
    if (output_path.begins_with("res://") || output_path.begins_with("user://")) {
        return ProjectSettings::get_singleton()->globalize_path(output_path);
    }
    String project_path = ProjectSettings::get_singleton()->globalize_path("res://");
    return project_path.path_join(output_path);
}

TemplateManager::TemplateManager() {
    remote_catalog_url = DEFAULT_TEMPLATE_CATALOG_URL;
    if (singleton == nullptr) {
        singleton = this;
    }
}

TemplateManager::~TemplateManager() {
    cancel_active_template_request();
    if (singleton == this) {
        singleton = nullptr;
    }
}

void TemplateManager::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load_versions_from_remote"), &TemplateManager::load_versions_from_remote);
    ClassDB::bind_method(D_METHOD("load_versions_from_remote_sync"), &TemplateManager::load_versions_from_remote_sync);
    ClassDB::bind_method(D_METHOD("load_versions_from_embedded"), &TemplateManager::load_versions_from_embedded);
    ClassDB::bind_method(D_METHOD("load_versions_from_local_cache"), &TemplateManager::load_versions_from_local_cache);
    ClassDB::bind_method(D_METHOD("get_available_versions"), &TemplateManager::get_available_versions);
    ClassDB::bind_method(D_METHOD("get_catalog_revision"), &TemplateManager::get_catalog_revision);
    ClassDB::bind_method(D_METHOD("get_remote_template_choices"), &TemplateManager::get_remote_template_choices);
    ClassDB::bind_method(D_METHOD("get_local_template_choices"), &TemplateManager::get_local_template_choices);
    ClassDB::bind_method(D_METHOD("get_active_template_info"), &TemplateManager::get_active_template_info);
    ClassDB::bind_method(D_METHOD("cache_template", "template_id"), &TemplateManager::cache_template);
    ClassDB::bind_method(D_METHOD("set_current_template", "template_id"), &TemplateManager::set_current_template);
    ClassDB::bind_method(D_METHOD("import_local_template", "path"), &TemplateManager::import_local_template);
    ClassDB::bind_method(D_METHOD("refresh_remote_catalog", "catalog_url"), &TemplateManager::refresh_remote_catalog);
    ClassDB::bind_method(D_METHOD("set_template_view", "view"), &TemplateManager::set_template_view);
    ClassDB::bind_method(D_METHOD("get_template_view"), &TemplateManager::get_template_view);
    ClassDB::bind_method(D_METHOD("get_remote_catalog_url"), &TemplateManager::get_remote_catalog_url);
    ClassDB::bind_method(D_METHOD("get_last_local_template_path"), &TemplateManager::get_last_local_template_path);
    ClassDB::bind_method(D_METHOD("resolve_active_template_path"), &TemplateManager::resolve_active_template_path);
    ClassDB::bind_method(D_METHOD("get_best_version_for_editor"), &TemplateManager::get_best_version_for_editor);
    ClassDB::bind_method(
            D_METHOD("resolve_template_filename_for_version", "target_version", "major_version"),
            &TemplateManager::resolve_template_filename_for_version,
            DEFVAL(""));
    ClassDB::bind_method(D_METHOD("download_active_template_async", "force_replace"), &TemplateManager::download_active_template_async, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("is_prefetch_active"), &TemplateManager::is_prefetch_active);
    ClassDB::bind_method(D_METHOD("get_download_status_text", "filename"), &TemplateManager::get_download_status_text);
    ClassDB::bind_method(D_METHOD("extract_template", "template_path", "output_path"), &TemplateManager::extract_template);
    ClassDB::bind_method(D_METHOD("get_current_godot_version"), &TemplateManager::get_current_godot_version);
    ClassDB::bind_method(D_METHOD("get_godot_major_version"), &TemplateManager::get_godot_major_version);

    // New methods for update detection
    ClassDB::bind_method(D_METHOD("initialize_template_system"), &TemplateManager::initialize_template_system);
    ClassDB::bind_method(D_METHOD("has_template_updates_available"), &TemplateManager::has_template_updates_available);
    ClassDB::bind_method(D_METHOD("get_missing_templates"), &TemplateManager::get_missing_templates);
    ClassDB::bind_method(D_METHOD("check_editor_template_status"), &TemplateManager::check_editor_template_status);

    // Individual template status
    ClassDB::bind_method(D_METHOD("is_template_embedded", "filename"), &TemplateManager::is_template_embedded);
    ClassDB::bind_method(D_METHOD("is_template_bundled", "filename"), &TemplateManager::is_template_bundled);
    ClassDB::bind_method(D_METHOD("is_template_downloaded", "filename"), &TemplateManager::is_template_downloaded);
    ClassDB::bind_method(D_METHOD("get_template_path", "filename"), &TemplateManager::get_template_path);
    ClassDB::bind_method(D_METHOD("get_bundled_template_path", "filename"), &TemplateManager::get_bundled_template_path);
    ClassDB::bind_method(D_METHOD("get_best_bundled_template_for_editor"), &TemplateManager::get_best_bundled_template_for_editor);
    ClassDB::bind_method(D_METHOD("get_best_available_template_for_editor"), &TemplateManager::get_best_available_template_for_editor);
    ClassDB::bind_method(
            D_METHOD("get_best_available_template_for_version", "target_version", "major_version"),
            &TemplateManager::get_best_available_template_for_version,
            DEFVAL(""));
    ClassDB::bind_method(D_METHOD("get_nearest_compatible_version", "target_version", "major_version"), &TemplateManager::get_nearest_compatible_version);
    ClassDB::bind_method(D_METHOD("remove_active_template_cache"), &TemplateManager::remove_active_template_cache);
    ClassDB::bind_method(D_METHOD("clear_all_template_cache"), &TemplateManager::clear_all_template_cache);
    ClassDB::bind_method(D_METHOD("refresh_versions"), &TemplateManager::refresh_versions);
    ClassDB::bind_method(D_METHOD("set_download_timeout", "timeout_seconds"), &TemplateManager::set_download_timeout);
    ClassDB::bind_method(D_METHOD("get_download_timeout"), &TemplateManager::get_download_timeout);

    ClassDB::bind_method(D_METHOD("_on_versions_download_completed"), &TemplateManager::_on_versions_download_completed);
    ClassDB::bind_method(
            D_METHOD("_on_template_download_request_completed", "result", "response_code", "headers", "body"),
            &TemplateManager::_on_template_download_request_completed);

    ADD_SIGNAL(MethodInfo("versions_loaded"));
    ADD_SIGNAL(MethodInfo("versions_refresh_failed", PropertyInfo(Variant::INT, "error_code")));
    ADD_SIGNAL(MethodInfo("template_download_finished", PropertyInfo(Variant::STRING, "filename"), PropertyInfo(Variant::BOOL, "success")));
    ADD_SIGNAL(MethodInfo("template_download_progress", PropertyInfo(Variant::STRING, "filename"), PropertyInfo(Variant::FLOAT, "progress")));
    ADD_SIGNAL(MethodInfo("active_template_changed", PropertyInfo(Variant::DICTIONARY, "template_info")));
    ADD_SIGNAL(MethodInfo("template_inventory_changed"));
}

TemplateManager* TemplateManager::get_singleton() {
    return singleton;
}

Error TemplateManager::load_versions_from_remote() {
    if (!Engine::get_singleton()->is_editor_hint()) {
        TOOLKIT_LOG("TemplateManager: load_versions_from_remote skipped outside editor.");
        return ERR_UNCONFIGURED;
    }

    if (refresh_in_flight) {
        return ERR_BUSY;
    }

    String versions_url = build_versions_url();
    if (versions_url.is_empty()) {
        TOOLKIT_LOG_RICH("[color=red]TemplateManager: Remote versions URL is empty, provider config is invalid.[/color]");
        return ERR_INVALID_PARAMETER;
    }
    TOOLKIT_LOG("TemplateManager: Downloading templates catalog from remote: ", versions_url);

    HTTPRequest* http_request = memnew(HTTPRequest);
    http_request->set_name("TemplateManager_VersionsRequest");
    NetworkProxy::apply(http_request);
    
    EditorInterface *editor = EditorInterface::get_singleton();
    Node* parent_node = editor ? editor->get_editor_main_screen() : nullptr;
    if (!parent_node) {
        TOOLKIT_LOG_RICH("[color=red]TemplateManager: Cannot get editor main screen to add HTTPRequest.[/color]");
        memdelete(http_request);
        return ERR_UNCONFIGURED;
    }
    parent_node->add_child(http_request);

    http_request->connect("request_completed", callable_mp(this, &TemplateManager::_on_versions_download_completed).bind(versions_url));

    Error err = http_request->request(versions_url);
    if (err != OK) {
        TOOLKIT_LOG_RICH("[color=red]TemplateManager: Failed to start templates catalog download request.[/color]");
        http_request->queue_free();
        return err;
    }

    refresh_in_flight = true;
    return OK;
}

Error TemplateManager::http_get_sync_follow_redirects(const String &url, PackedByteArray &r_body, int &r_response_code, Dictionary *r_response_headers, const std::function<void(int64_t downloaded, int64_t total)> &progress_callback) const {
    String current_url = url.strip_edges();
    if (current_url.is_empty()) {
        r_response_code = 0;
        return ERR_INVALID_PARAMETER;
    }

    constexpr int max_redirects = 5;
    for (int redirect_count = 0; redirect_count <= max_redirects; redirect_count++) {
        String host;
        String request_path;
        int port = -1;
        bool use_tls = false;
        if (!_parse_http_url(current_url, host, port, request_path, use_tls)) {
            r_response_code = 0;
            return ERR_INVALID_PARAMETER;
        }

        Ref<HTTPClient> client = memnew(HTTPClient);
        NetworkProxy::apply(client);
        Ref<TLSOptions> tls_options;
        if (use_tls) {
            tls_options = TLSOptions::client();
        }

        Error connect_err = client->connect_to_host(host, port, tls_options);
        if (connect_err != OK) {
            r_response_code = 0;
            return connect_err;
        }

        uint64_t connect_deadline_ms = Time::get_singleton()->get_ticks_msec() + uint64_t(download_timeout) * 1000;
        while (true) {
            if (Time::get_singleton()->get_ticks_msec() > connect_deadline_ms) {
                client->close();
                r_response_code = 0;
                return ERR_TIMEOUT;
            }

            Error poll_err = client->poll();
            if (poll_err != OK) {
                client->close();
                r_response_code = 0;
                return poll_err;
            }

            HTTPClient::Status status = client->get_status();
            if (status == HTTPClient::STATUS_CONNECTED) {
                break;
            }
            if (status == HTTPClient::STATUS_CANT_CONNECT ||
                    status == HTTPClient::STATUS_CANT_RESOLVE ||
                    status == HTTPClient::STATUS_CONNECTION_ERROR ||
                    status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR) {
                client->close();
                r_response_code = 0;
                return ERR_CANT_CONNECT;
            }

            OS::get_singleton()->delay_msec(16);
        }

        PackedStringArray headers;
        headers.append("User-Agent: GodotMinigame/1.0");
        headers.append("Accept: */*");
        Error request_err = client->request(HTTPClient::METHOD_GET, request_path, headers);
        if (request_err != OK) {
            client->close();
            r_response_code = 0;
            return request_err;
        }

        uint64_t response_deadline_ms = Time::get_singleton()->get_ticks_msec() + uint64_t(download_timeout) * 1000;
        while (!client->has_response()) {
            if (Time::get_singleton()->get_ticks_msec() > response_deadline_ms) {
                client->close();
                r_response_code = 0;
                return ERR_TIMEOUT;
            }

            Error poll_err = client->poll();
            if (poll_err != OK) {
                client->close();
                r_response_code = 0;
                return poll_err;
            }

            OS::get_singleton()->delay_msec(16);
        }

        r_response_code = client->get_response_code();
        Dictionary response_headers = client->get_response_headers_as_dictionary();

        if (_is_redirect_response_code(r_response_code)) {
            String location = _get_header_value_case_insensitive(response_headers, "Location");
            client->close();
            if (location.is_empty()) {
                return ERR_CANT_CONNECT;
            }
            current_url = _build_absolute_redirect_url(current_url, location);
            continue;
        }

        if (r_response_code != HTTPClient::RESPONSE_OK) {
            client->close();
            if (r_response_code == HTTPClient::RESPONSE_NOT_FOUND) {
                return ERR_FILE_NOT_FOUND;
            }
            return ERR_CANT_CONNECT;
        }

        PackedByteArray response_body;
        int64_t total = client->get_response_body_length();
        int64_t downloaded = 0;
        uint64_t body_idle_deadline_ms = Time::get_singleton()->get_ticks_msec() + uint64_t(download_timeout) * 1000;

        while (true) {
            if (Time::get_singleton()->get_ticks_msec() > body_idle_deadline_ms) {
                client->close();
                return ERR_TIMEOUT;
            }

            Error poll_err = client->poll();
            if (poll_err != OK) {
                client->close();
                return poll_err;
            }

            PackedByteArray chunk = client->read_response_body_chunk();
            if (!chunk.is_empty()) {
                int old_size = response_body.size();
                response_body.resize(old_size + chunk.size());
                for (int i = 0; i < chunk.size(); i++) {
                    response_body.set(old_size + i, chunk[i]);
                }
                downloaded += chunk.size();
                body_idle_deadline_ms = Time::get_singleton()->get_ticks_msec() + uint64_t(download_timeout) * 1000;
                if (progress_callback) {
                    progress_callback(downloaded, total);
                }
            }

            HTTPClient::Status status = client->get_status();
            if (status == HTTPClient::STATUS_CONNECTED) {
                break;
            }
            if (status == HTTPClient::STATUS_CANT_CONNECT ||
                    status == HTTPClient::STATUS_CANT_RESOLVE ||
                    status == HTTPClient::STATUS_CONNECTION_ERROR ||
                    status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR) {
                client->close();
                return ERR_CANT_CONNECT;
            }

            if (chunk.is_empty()) {
                if (total > 0 && downloaded >= total) {
                    break;
                }
                OS::get_singleton()->delay_msec(8);
            }
        }

        client->close();
        r_body = response_body;
        if (r_response_headers != nullptr) {
            *r_response_headers = response_headers;
        }
        return OK;
    }

    r_response_code = 0;
    return ERR_CANT_CONNECT;
}

Error TemplateManager::load_versions_from_remote_sync() {
    String versions_url = build_versions_url();
    if (versions_url.is_empty()) {
        TOOLKIT_LOG_RICH("[color=red]TemplateManager: Remote versions URL is empty, provider config is invalid.[/color]");
        return ERR_INVALID_PARAMETER;
    }
    TOOLKIT_LOG("TemplateManager: Loading versions synchronously from ", versions_url);

    PackedByteArray response_body;
    int response_code = 0;
    Error request_err = ERR_CANT_CONNECT;
    for (int attempt = 0; attempt < 5; attempt++) {
        request_err = http_get_sync_follow_redirects(versions_url, response_body, response_code);
        if (request_err == OK || request_err == ERR_FILE_NOT_FOUND || request_err == ERR_INVALID_PARAMETER) {
            break;
        }
        OS::get_singleton()->delay_msec(250 * (attempt + 1));
    }
    if (request_err != OK) {
        TOOLKIT_LOG("TemplateManager: Synchronous versions request failed. error=", request_err, ", response_code=", response_code);
        return request_err;
    }
    TOOLKIT_LOG("TemplateManager: Synchronous versions request completed. response_code=", response_code, ", body_size=", response_body.size());

    String catalog_content = response_body.get_string_from_utf8();
    Error parse_err = parse_templates_catalog(catalog_content);
    if (parse_err != OK) {
        TOOLKIT_LOG("TemplateManager: Failed to parse remote versions synchronously. parse_err=", parse_err);
        return parse_err;
    }

    TOOLKIT_LOG("TemplateManager: Remote templates catalog parsed successfully. available_versions=", available_versions);

    return save_versions_to_local_cache();
}

Error TemplateManager::load_versions_from_local_cache() {
    String local_versions_path = get_local_versions_cache_path();

    if (!FileAccess::file_exists(local_versions_path)) {
        TOOLKIT_LOG("TemplateManager: No local versions cache found at ", local_versions_path);
        return ERR_FILE_NOT_FOUND;
    }

    Ref<FileAccess> file = FileAccess::open(local_versions_path, FileAccess::READ);
    if (file.is_null()) {
        TOOLKIT_LOG_RICH("[color=red]Error: Cannot read local versions cache[/color]");
        return ERR_FILE_CANT_READ;
    }

    String catalog_content = file->get_as_text();
    file->close();

    Error result = parse_templates_catalog(catalog_content);
    if (result == OK) {
        TOOLKIT_LOG("TemplateManager: Loaded versions from local cache");
    }
    return result;
}

Error TemplateManager::load_versions_from_embedded() {
#ifdef EMBED_RESOURCES
    for (int i = 0; toolkit::resources::embedded_resources[i].path != nullptr; i++) {
        const auto& resource = toolkit::resources::embedded_resources[i];
        if (String(resource.path) == "catalog/templates.json") {
            String catalog_content = String::utf8((const char*)resource.data, resource.size);
            return parse_templates_catalog(catalog_content);
        }
    }
#endif

    const char *catalog_paths[] = {
        "res://addons/godot-minigame/catalog/templates.json",
        nullptr
    };

    for (int i = 0; catalog_paths[i] != nullptr; i++) {
        Ref<FileAccess> file = FileAccess::open(String::utf8(catalog_paths[i]), FileAccess::READ);
        if (file.is_valid()) {
            String catalog_content = file->get_as_text();
            file->close();
            Error result = parse_templates_catalog(catalog_content);
            if (result == OK) {
                return OK;
            }
        }
    }

    TOOLKIT_LOG_RICH("[color=yellow]Warning: catalog/templates.json not found[/color]");
    return ERR_FILE_NOT_FOUND;
}

Error TemplateManager::parse_templates_catalog(const String& json_content) {
    Variant root_variant = JSON::parse_string(json_content);
    if (root_variant.get_type() != Variant::DICTIONARY) {
        return ERR_PARSE_ERROR;
    }

    Dictionary root = root_variant;
    if (int(root.get("schema_version", 0)) != 1) {
        return ERR_INVALID_DATA;
    }
    Variant templates_variant = root.get("templates", Variant());
    if (templates_variant.get_type() != Variant::ARRAY) {
        return ERR_INVALID_DATA;
    }

    Dictionary parsed_versions;
    Array parsed_available;
    Array accepted_templates;
    Array templates = templates_variant;
    for (int i = 0; i < templates.size(); i++) {
        if (templates[i].get_type() != Variant::DICTIONARY) {
            return ERR_INVALID_DATA;
        }
        Dictionary entry = templates[i];
        String status = String(entry.get("status", "")).strip_edges().to_lower();
        String minimum_plugin = String(entry.get("minimum_plugin", "")).strip_edges();
        if (status != "stable" || !_is_semver(minimum_plugin) ||
                compare_version_numbers(minimum_plugin, TOOLKIT_PLUGIN_VERSION) > 0) {
            continue;
        }

        String major = String(entry.get("godot_major", "")).strip_edges();
        String version = String(entry.get("godot_version", "")).strip_edges();
        String filename = String(entry.get("file", "")).strip_edges();
        String release_tag = String(entry.get("tag", "")).strip_edges();
        String sha256 = String(entry.get("sha256", "")).strip_edges().to_lower();
        String download_url = String(entry.get("download_url", "")).strip_edges();
        const String download_filename = download_url.get_slice("?", 0).get_file();
        if (major.is_empty() || version.is_empty() || filename.is_empty() ||
                !filename.ends_with(".tpz") || filename.get_file() != filename ||
                release_tag.is_empty() || !_is_sha256(sha256) ||
                (!download_url.begins_with("https://") && !download_url.begins_with("http://")) ||
                download_filename != filename) {
            return ERR_INVALID_DATA;
        }

        Dictionary normalized_entry;
        normalized_entry["file"] = filename;
        normalized_entry["tag"] = release_tag;
        normalized_entry["sha256"] = sha256;
        normalized_entry["download_url"] = download_url;
        normalized_entry["minimum_plugin"] = minimum_plugin;
        normalized_entry["status"] = status;

        Dictionary major_versions;
        if (parsed_versions.has(major)) {
            major_versions = parsed_versions[major];
        }
        major_versions[version] = normalized_entry;
        parsed_versions[major] = major_versions;

        Dictionary version_info;
        version_info["godot_major"] = major;
        version_info["version"] = version;
        version_info["filename"] = filename;
        version_info["release_tag"] = release_tag;
        version_info["sha256"] = sha256;
        version_info["download_url"] = download_url;
        version_info["minimum_plugin"] = minimum_plugin;
        version_info["is_embedded"] = is_template_embedded(filename);
        parsed_available.append(version_info);
        accepted_templates.append(entry.duplicate(true));
    }

    if (parsed_available.is_empty()) {
        return ERR_INVALID_DATA;
    }

    Dictionary accepted_catalog;
    accepted_catalog["schema_version"] = 1;
    accepted_catalog["templates"] = accepted_templates;

    versions_cache = parsed_versions;
    available_versions = parsed_available;
    catalog_cache = accepted_catalog;
    versions_loaded = true;
    catalog_revision++;
    TOOLKIT_LOG("TemplateManager: Accepted ", available_versions.size(), " stable compatible catalog template(s)");
    return OK;
}

Array TemplateManager::get_available_versions() const {
    if (available_versions.is_empty() && !versions_cache.is_empty()) {
        Array rebuilt = build_available_versions_from_cache();
        if (!rebuilt.is_empty()) {
            TOOLKIT_LOG("TemplateManager: Rebuilt available_versions from versions_cache on demand: ", rebuilt);
            return rebuilt;
        }
    }
    return available_versions;
}

Dictionary TemplateManager::get_versions_data() const {
    return versions_cache;
}

int64_t TemplateManager::get_catalog_revision() const {
    return catalog_revision;
}

String TemplateManager::get_editor_version_line() const {
    PackedStringArray parts = get_current_godot_version().split(".");
    if (parts.size() < 2) {
        return get_current_godot_version();
    }
    return String(parts[0]) + "." + String(parts[1]);
}

Array TemplateManager::get_remote_template_choices() const {
    Array choices;
    const String editor_line = get_editor_version_line();
    const String editor_major = get_godot_major_version();
    for (int i = 0; i < available_versions.size(); i++) {
        Dictionary info = available_versions[i];
        const String version = String(info.get("version", ""));
        if (String(info.get("godot_major", "")) != editor_major || !version.begins_with(editor_line + String("."))) {
            continue;
        }
        Dictionary choice = info.duplicate(true);
        const String filename = String(choice.get("filename", ""));
        const bool cached = is_template_downloaded(filename);
        const String bundled_path = get_bundled_template_path(filename);
        const bool embedded = is_template_embedded(filename);
        choice["id"] = "remote:" + String(choice.get("sha256", "")).to_lower();
        choice["kind"] = "catalog";
        choice["provider"] = "remote";
        choice["origin"] = String::utf8("远端缓存");
        choice["display_name"] = "Godot " + version;
        choice["cached"] = cached;
        choice["bundled"] = !bundled_path.is_empty() || embedded;
        choice["available"] = cached || !bundled_path.is_empty() || embedded;
        choice["path"] = cached ? get_download_cache_path(filename) : (!bundled_path.is_empty() ? bundled_path : (embedded ? "embedded://" + filename : String()));
        choices.append(choice);
    }
    return choices;
}

void TemplateManager::collect_local_template_files(
        const String &root,
        const String &fallback_origin,
        Array &choices,
        Dictionary &seen_paths) const {
    Ref<DirAccess> dir = DirAccess::open(root);
    if (dir.is_null()) {
        return;
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
        const String path = root.path_join(name).simplify_path();
        if (dir->current_is_dir()) {
            collect_local_template_files(path, fallback_origin, choices, seen_paths);
            continue;
        }
        if (name.get_extension().to_lower() != "tpz" || seen_paths.has(path) || !validate_template_archive(path)) {
            continue;
        }

        String origin = fallback_origin;
        String provider = fallback_origin == String::utf8("远端缓存") ? String("remote") : String("local");

        Dictionary choice;
        String choice_id = "local:" + path.md5_text();
        String display_name = name;
        String version;
        String source;
        const String actual_sha = FileAccess::get_sha256(path).to_lower();
        if (provider == "remote") {
            for (int i = 0; i < available_versions.size(); i++) {
                const Dictionary catalog_entry = available_versions[i];
                const String catalog_filename = String(catalog_entry.get("filename", ""));
                if (catalog_filename.is_empty() || !name.ends_with(catalog_filename)) {
                    continue;
                }
                version = String(catalog_entry.get("version", ""));
                display_name = version.is_empty() ? name : "Godot " + version;
                const String expected_sha = String(catalog_entry.get("sha256", "")).strip_edges().to_lower();
                if (!expected_sha.is_empty() && actual_sha != expected_sha) {
                    origin = String::utf8("本地修改");
                    provider = "local";
                }
                break;
            }
        }
        const Array record_ids = local_template_records.keys();
        for (int i = 0; i < record_ids.size(); i++) {
            const String record_id = String(record_ids[i]);
            const Dictionary record = local_template_records.get(record_id, Dictionary());
            if (String(record.get("path", "")).simplify_path() != path) {
                continue;
            }
            choice_id = record_id;
            display_name = String(record.get("display_name", display_name));
            version = String(record.get("version", ""));
            source = String(record.get("source", ""));
            const String recorded_sha = String(record.get("sha256", "")).to_lower();
            if (!recorded_sha.is_empty() && recorded_sha != actual_sha) {
                origin = String::utf8("本地修改");
                provider = "local";
            } else {
                origin = String(record.get("origin", origin));
                provider = String(record.get("provider", provider));
            }
            break;
        }
        choice["id"] = choice_id;
        choice["kind"] = "local";
        choice["path"] = path;
        choice["filename"] = name;
        choice["display_name"] = display_name;
        choice["origin"] = origin;
        choice["provider"] = provider;
        choice["source"] = source;
        choice["version"] = version;
        choice["cached"] = true;
        choice["bundled"] = fallback_origin == String::utf8("插件内置");
        choice["available"] = true;
        choices.append(choice);
        seen_paths[path] = true;
    }
    dir->list_dir_end();
}

void TemplateManager::migrate_legacy_template_cache() {
    Array legacy_choices;
    Dictionary seen_paths;
    collect_local_template_files(get_global_template_cache_root().path_join("sources"), String::utf8("远端缓存"), legacy_choices, seen_paths);
    collect_local_template_files(get_global_template_cache_root().path_join("custom"), String::utf8("本地导入"), legacy_choices, seen_paths);
    if (legacy_choices.is_empty()) {
        return;
    }
    const String artifact_dir = get_global_template_cache_root().path_join("artifacts");
    if (!UserDataPath::create_directory_if_not_exists(artifact_dir)) {
        return;
    }
    bool changed = false;
    for (int i = 0; i < legacy_choices.size(); i++) {
        Dictionary choice = legacy_choices[i];
        const String source_path = String(choice.get("path", ""));
        const String sha256 = FileAccess::get_sha256(source_path).to_lower();
        if (sha256.is_empty()) {
            continue;
        }
        const String output_path = artifact_dir.path_join(sha256 + String(".tpz"));
        if (!FileAccess::file_exists(output_path)) {
            const String temporary_path = output_path + String(".migrate.") + String::num_int64(OS::get_singleton()->get_process_id());
            DirAccess::remove_absolute(temporary_path);
            Error err = DirAccess::copy_absolute(source_path, temporary_path);
            if (err == OK && validate_template_archive(temporary_path) && FileAccess::get_sha256(temporary_path).to_lower() == sha256) {
                err = publish_download_atomically(temporary_path, output_path);
            }
            if (err != OK) {
                DirAccess::remove_absolute(temporary_path);
                continue;
            }
        }
        choice["id"] = "local:" + sha256;
        choice["path"] = output_path;
        choice["sha256"] = sha256;
        register_local_template(choice, output_path);
        changed = true;
    }
    if (changed) {
        persist_active_template_selection();
    }
}

Array TemplateManager::get_local_template_choices() const {
    Array choices;
    Dictionary seen_paths;

    const Array catalog_choices = get_remote_template_choices();
    for (int i = 0; i < catalog_choices.size(); i++) {
        const Dictionary catalog_choice = catalog_choices[i];
        if (!bool(catalog_choice.get("available", false))) {
            continue;
        }
        const String path = String(catalog_choice.get("path", ""));
        if (path.is_empty() || seen_paths.has(path)) {
            continue;
        }
        Dictionary local_choice = catalog_choice.duplicate(true);
        local_choice["kind"] = "local";
        const String origin = bool(local_choice.get("bundled", false))
                ? String::utf8("插件内置")
                : String::utf8("远端缓存");
        local_choice["origin"] = origin;
        choices.append(local_choice);
        seen_paths[path] = true;
    }

    collect_local_template_files(get_global_template_cache_root().path_join("artifacts"), String::utf8("本地缓存"), choices, seen_paths);
    collect_local_template_files(get_global_template_cache_root().path_join("sources"), String::utf8("远端缓存"), choices, seen_paths);
    collect_local_template_files(get_global_template_cache_root().path_join("custom"), String::utf8("本地导入"), choices, seen_paths);
    collect_local_template_files("res://addons/godot-minigame/resources/templates", String::utf8("插件内置"), choices, seen_paths);
    return choices;
}

String TemplateManager::get_custom_template_cache_path(const String &url) const {
    const String filename = url.get_slice("?", 0).get_file();
    return get_global_template_cache_root().path_join("custom").path_join(url.md5_text() + "-" + filename);
}

String TemplateManager::get_active_template_filename() const {
    if (active_template_kind == "custom") {
        return active_custom_url.is_empty() ? String() : get_custom_template_cache_path(active_custom_url).get_file();
    }
    for (int i = 0; i < available_versions.size(); i++) {
        Dictionary info = available_versions[i];
        if (String(info.get("version", "")) == active_template_version &&
                String(info.get("godot_major", "")) == get_godot_major_version()) {
            return String(info.get("filename", ""));
        }
    }
    return "";
}

Dictionary TemplateManager::get_active_template_info() const {
    Dictionary info;
    if (!active_template_id.is_empty()) {
        String resolved_origin = active_template_origin;
        const Dictionary active_record = local_template_records.get(active_template_id, Dictionary());
        if (!active_record.is_empty() && FileAccess::file_exists(active_template_path)) {
            const String recorded_sha = String(active_record.get("sha256", "")).to_lower();
            if (!recorded_sha.is_empty() && FileAccess::get_sha256(active_template_path).to_lower() != recorded_sha) {
                resolved_origin = String::utf8("本地修改");
            }
        }
        info["id"] = active_template_id;
        info["pending_id"] = String(pending_template.get("id", ""));
        info["kind"] = "local";
        info["version"] = active_template_version;
        info["url"] = active_custom_url;
        info["source"] = resolved_origin;
        info["origin"] = resolved_origin;
        info["path"] = active_template_path;
        info["filename"] = active_template_path.get_file();
        info["display_name"] = active_template_display_name.is_empty() ? active_template_path.get_file() : active_template_display_name;
        const bool bundled = active_template_path.begins_with("res://") || active_template_path.begins_with("embedded://");
        info["bundled"] = bundled;
        info["cached"] = !bundled;
        info["available"] = !resolve_active_template_path().is_empty();
        return info;
    }
    info["kind"] = active_template_kind;
    info["id"] = "";
    info["pending_id"] = String(pending_template.get("id", ""));
    info["version"] = active_template_version;
    info["url"] = active_custom_url;
    info["source"] = active_template_kind == "custom" ? (active_custom_url.is_absolute_path() ? String("local") : String("remote")) : String("catalog");
    const String filename = get_active_template_filename();
    info["filename"] = filename;
    if (active_template_kind == "custom") {
        const String path = active_custom_url.is_empty() ? String() : get_custom_template_cache_path(active_custom_url);
        info["cached"] = !path.is_empty() && FileAccess::file_exists(path);
        info["bundled"] = false;
        info["display_name"] = filename.is_empty() ? String::utf8("未选择自定义模板") : filename;
    } else {
        info["cached"] = !filename.is_empty() && is_template_downloaded(filename);
        info["bundled"] = !filename.is_empty() && (is_template_bundled(filename) || is_template_embedded(filename));
        info["display_name"] = active_template_version.is_empty() ? String::utf8("未选择模板") : "Godot " + active_template_version;
    }
    info["available"] = !resolve_active_template_path().is_empty();
    return info;
}

Dictionary TemplateManager::find_template_choice(const String &template_id, bool include_local) const {
    Array choices = get_remote_template_choices();
    for (int i = 0; i < choices.size(); i++) {
        Dictionary choice = choices[i];
        if (String(choice.get("id", "")) == template_id) {
            return choice;
        }
    }
    if (include_local) {
        choices = get_local_template_choices();
        for (int i = 0; i < choices.size(); i++) {
            Dictionary choice = choices[i];
            if (String(choice.get("id", "")) == template_id) {
                return choice;
            }
        }
    }
    return Dictionary();
}

void TemplateManager::register_local_template(const Dictionary &choice, const String &path) {
    if (path.begins_with("embedded://")) {
        return;
    }
    const String id = String(choice.get("id", "local:" + path.md5_text()));
    Dictionary record;
    record["path"] = path.simplify_path();
    record["display_name"] = String(choice.get("display_name", path.get_file()));
    record["origin"] = String(choice.get("origin", String::utf8("本地")));
    record["provider"] = String(choice.get("provider", "local"));
    record["source"] = String(choice.get("source", ""));
    record["version"] = String(choice.get("version", ""));
    record["sha256"] = FileAccess::file_exists(path) ? FileAccess::get_sha256(path) : String();
    local_template_records[id] = record;
}

Error TemplateManager::activate_local_template(const Dictionary &choice, const String &path) {
    const bool embedded = path.begins_with("embedded://");
    if (path.is_empty() || (!embedded && (!FileAccess::file_exists(path) || !validate_template_archive(path)))) {
        return ERR_FILE_CORRUPT;
    }
    Dictionary local_choice = choice.duplicate(true);
    const bool bundled = embedded || path.begins_with("res://");
    if (bundled) {
        local_choice["origin"] = String::utf8("插件内置");
    } else if (String(local_choice.get("kind", "")) == "catalog") {
        local_choice["origin"] = String::utf8("远端缓存");
    }
    active_template_id = String(local_choice.get("id", "local:" + path.md5_text()));
    active_template_path = embedded ? path : path.simplify_path();
    active_template_display_name = String(local_choice.get("display_name", path.get_file()));
    active_template_origin = String(local_choice.get("origin", String::utf8("本地")));
    active_template_version = String(local_choice.get("version", ""));
    active_custom_url = String(local_choice.get("source", ""));
    active_template_kind = "local";
    register_local_template(local_choice, active_template_path);
    persist_active_template_selection();
    emit_signal("active_template_changed", get_active_template_info());
    emit_signal("template_inventory_changed");
    return OK;
}

void TemplateManager::complete_pending_template(const String &path) {
    if (pending_template.is_empty()) {
        return;
    }
    const Dictionary completed = pending_template.duplicate(true);
    const bool activate_after_download = pending_template_activate;
    pending_template.clear();
    pending_template_activate = true;
    if (activate_after_download) {
        activate_local_template(completed, path);
        return;
    }
    Dictionary local_choice = completed.duplicate(true);
    local_choice["kind"] = "local";
    local_choice["origin"] = String::utf8("远端缓存");
    register_local_template(local_choice, path);
    persist_active_template_selection();
    emit_signal("template_inventory_changed");
}

Error TemplateManager::cache_template(const String &template_id) {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }
    Dictionary choice = find_template_choice(template_id.strip_edges(), false);
    if (choice.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    const String path = String(choice.get("path", ""));
    if (bool(choice.get("available", false)) && !path.is_empty()) {
        emit_signal("template_inventory_changed");
        return OK;
    }
    if (String(choice.get("kind", "")) != "catalog") {
        return ERR_FILE_NOT_FOUND;
    }
    pending_template = choice.duplicate(true);
    pending_template_activate = false;
    const Error err = download_active_template_async(false);
    if (err != OK) {
        pending_template.clear();
        pending_template_activate = true;
    }
    return err;
}

Error TemplateManager::set_current_template(const String &template_id) {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }
    Dictionary choice = find_template_choice(template_id.strip_edges(), true);
    if (choice.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    const String path = String(choice.get("path", ""));
    if (bool(choice.get("available", false)) && !path.is_empty()) {
        return activate_local_template(choice, path);
    }
    if (String(choice.get("kind", "")) != "catalog") {
        return ERR_FILE_NOT_FOUND;
    }
    pending_template = choice;
    pending_template_activate = true;
    const Error err = download_active_template_async(false);
    if (err != OK) {
        pending_template.clear();
    }
    return err;
}

Dictionary TemplateManager::make_imported_template_choice(const String &source_path, const String &cached_path) const {
    const String sha256 = FileAccess::get_sha256(cached_path).to_lower();
    Dictionary choice;
    choice["id"] = "local:" + sha256;
    choice["kind"] = "local";
    choice["path"] = cached_path;
    choice["filename"] = source_path.get_file();
    choice["display_name"] = source_path.get_file();
    choice["origin"] = String::utf8("本地导入");
    choice["provider"] = "local";
    choice["source"] = source_path;
    choice["version"] = "";
    choice["sha256"] = sha256;
    choice["cached"] = true;
    choice["bundled"] = false;
    choice["available"] = true;
    for (int i = 0; i < available_versions.size(); i++) {
        const Dictionary remote = available_versions[i];
        if (String(remote.get("sha256", "")).to_lower() != sha256) {
            continue;
        }
        const String version = String(remote.get("version", ""));
        choice["version"] = version;
        choice["display_name"] = version.is_empty() ? source_path.get_file() : "Godot " + version;
        break;
    }
    return choice;
}

Error TemplateManager::import_local_template(const String &path) {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }
    const String source_path = path.strip_edges().simplify_path();
    if (!source_path.is_absolute_path() || source_path.get_extension().to_lower() != "tpz" ||
            !FileAccess::file_exists(source_path) || !validate_template_archive(source_path)) {
        return ERR_INVALID_PARAMETER;
    }
    const String sha256 = FileAccess::get_sha256(source_path).to_lower();
    const String import_dir = get_global_template_cache_root().path_join("artifacts");
    if (!UserDataPath::create_directory_if_not_exists(import_dir)) {
        return ERR_FILE_CANT_WRITE;
    }
    const String output_path = import_dir.path_join(sha256 + String(".tpz"));
    if (!FileAccess::file_exists(output_path) || FileAccess::get_sha256(output_path).to_lower() != sha256 ||
            !validate_template_archive(output_path)) {
        const String temporary_path = output_path + String(".import.") + String::num_int64(OS::get_singleton()->get_process_id());
        DirAccess::remove_absolute(temporary_path);
        Error err = DirAccess::copy_absolute(source_path, temporary_path);
        if (err == OK && validate_template_archive(temporary_path) && FileAccess::get_sha256(temporary_path).to_lower() == sha256) {
            err = publish_download_atomically(temporary_path, output_path);
        }
        if (err != OK) {
            DirAccess::remove_absolute(temporary_path);
            return err;
        }
    }
    last_local_template_path = source_path;
    const Dictionary choice = make_imported_template_choice(source_path, output_path);
    register_local_template(choice, output_path);
    persist_active_template_selection();
    emit_signal("template_inventory_changed");
    return OK;
}

Error TemplateManager::refresh_remote_catalog(const String &catalog_url) {
    if (refresh_in_flight || is_prefetch_active()) {
        return ERR_BUSY;
    }
    const String normalized = catalog_url.strip_edges();
    if ((!normalized.begins_with("https://") && !normalized.begins_with("http://")) ||
            normalized.get_slice("?", 0).get_extension().to_lower() != "json") {
        return ERR_INVALID_PARAMETER;
    }
    pending_catalog_url = normalized;
    const Error err = load_versions_from_remote();
    if (err != OK) {
        pending_catalog_url = "";
    }
    return err;
}

void TemplateManager::set_template_view(const String &view) {
    if (is_prefetch_active()) {
        return;
    }
    const String normalized = view.strip_edges().to_lower();
    if (normalized != "remote" && normalized != "local") {
        return;
    }
    template_view = normalized;
    persist_active_template_selection();
}

String TemplateManager::get_template_view() const {
    return template_view;
}

String TemplateManager::get_remote_catalog_url() const {
    return remote_catalog_url;
}

String TemplateManager::resolve_active_template_path() const {
    if (!active_template_id.is_empty()) {
        if (active_template_path.begins_with("embedded://")) {
            return active_template_path;
        }
        return FileAccess::file_exists(active_template_path) && validate_template_archive(active_template_path)
                ? active_template_path
                : String();
    }
    if (active_template_kind == "custom") {
        if (active_custom_url.is_empty()) {
            return "";
        }
        const String path = get_custom_template_cache_path(active_custom_url);
        return FileAccess::file_exists(path) && validate_template_archive(path) ? path : String();
    }
    const String filename = get_active_template_filename();
    if (filename.is_empty()) {
        return "";
    }
    const String path = get_template_path(filename);
    return path.begins_with("remote://") ? String() : path;
}

Dictionary TemplateManager::normalize_template_entry(const Variant &entry, const String &fallback_release_tag) const {
    Dictionary normalized;
    normalized["filename"] = "";
    normalized["release_tag"] = fallback_release_tag.strip_edges();
    normalized["sha256"] = "";
    normalized["download_url"] = "";

    if (entry.get_type() == Variant::STRING) {
        normalized["filename"] = String(entry).strip_edges();
        return normalized;
    }

    if (entry.get_type() == Variant::DICTIONARY) {
        Dictionary dict_entry = entry;
        normalized["filename"] = String(dict_entry.get("file", dict_entry.get("filename", ""))).strip_edges();
        normalized["release_tag"] = String(dict_entry.get("tag", dict_entry.get("release_tag", fallback_release_tag))).strip_edges();
        normalized["sha256"] = String(dict_entry.get("sha256", "")).strip_edges().to_lower();
        normalized["download_url"] = String(dict_entry.get("download_url", "")).strip_edges();
    }

    return normalized;
}

String TemplateManager::find_release_tag_for_filename(const String &filename) const {
    Array versions = get_available_versions();
    for (int i = 0; i < versions.size(); i++) {
        Dictionary version_info = versions[i];
        if (String(version_info.get("filename", "")) == filename) {
            String release_tag = String(version_info.get("release_tag", "")).strip_edges();
            if (!release_tag.is_empty()) {
                return release_tag;
            }
        }
    }
    return "";
}

String TemplateManager::find_sha256_for_filename(const String &filename) const {
    Array versions = get_available_versions();
    for (int i = 0; i < versions.size(); i++) {
        Dictionary version_info = versions[i];
        if (String(version_info.get("filename", "")) == filename) {
            return String(version_info.get("sha256", "")).strip_edges().to_lower();
        }
    }
    return "";
}

bool TemplateManager::verify_template_file(const String &filename, const String &path) const {
    String expected = find_sha256_for_filename(filename);
    if (expected.is_empty() || !FileAccess::file_exists(path)) {
        return false;
    }
    String actual = FileAccess::get_sha256(path).to_lower();
    if (actual == expected) {
        return true;
    }
    TOOLKIT_LOG_RICH("[color=red]TemplateManager: SHA-256 mismatch for ", filename,
            ". expected=", expected, ", actual=", actual, "[/color]");
    return false;
}

String TemplateManager::get_best_version_for_editor() const {
    String current_version = get_current_godot_version();
    String major_version = get_godot_major_version();

    TOOLKIT_LOG("TemplateManager: Finding best version for editor ", current_version, " (major: ", major_version, ")");

    // First try exact match
    if (has_version(major_version, current_version)) {
        String exact_filename = get_template_filename(major_version, current_version);
        TOOLKIT_LOG("TemplateManager: Found exact match filename: '", exact_filename, "'");
        // Check if template is actually available (embedded, cached, or remote)
        if (!exact_filename.is_empty() && is_template_available_anywhere(exact_filename)) {
            TOOLKIT_LOG("TemplateManager: Exact match is available");
            return exact_filename;
        }
    }

    // Prefer the latest template from the same major.minor line before falling back
    // to older minors. Example: 4.5.0 should prefer 4.5.1 over 4.4.x.
    String same_minor_filename = get_latest_version_for_minor_line(current_version, major_version);
    TOOLKIT_LOG("TemplateManager: Same minor-line filename: '", same_minor_filename, "'");
    if (!same_minor_filename.is_empty() && is_template_available_anywhere(same_minor_filename)) {
        TOOLKIT_LOG("TemplateManager: Same minor-line match is available");
        return same_minor_filename;
    }

    // Try nearest compatible version using just-close matching principle
    String nearest_filename = get_nearest_compatible_version(current_version, major_version);
    TOOLKIT_LOG("TemplateManager: Nearest compatible filename: '", nearest_filename, "'");
    if (!nearest_filename.is_empty() && is_template_available_anywhere(nearest_filename)) {
        TOOLKIT_LOG("TemplateManager: Nearest compatible is available");
        return nearest_filename;
    }

    // Fall back to latest version for this major version
    String latest_filename = get_latest_version_for_godot_major(major_version);
    TOOLKIT_LOG("TemplateManager: Latest version filename: '", latest_filename, "'");
    return latest_filename;
}

String TemplateManager::resolve_template_filename_for_version(const String &target_version, const String &major_version) const {
    String resolved_target_version = target_version.strip_edges();
    if (resolved_target_version.is_empty()) {
        resolved_target_version = get_current_godot_version();
    }

    String resolved_major_version = major_version.strip_edges();
    if (resolved_major_version.is_empty()) {
        PackedStringArray version_parts = resolved_target_version.split(".");
        if (!version_parts.is_empty() && !String(version_parts[0]).strip_edges().is_empty()) {
            resolved_major_version = "godot" + String(version_parts[0]).strip_edges();
        } else {
            resolved_major_version = get_godot_major_version();
        }
    }

    TOOLKIT_LOG("TemplateManager: Resolving template filename for version ", resolved_target_version, " (major: ", resolved_major_version, ")");

    if (has_version(resolved_major_version, resolved_target_version)) {
        String exact_filename = get_template_filename(resolved_major_version, resolved_target_version);
        if (!exact_filename.is_empty()) {
            return exact_filename;
        }
    }

    String same_minor_filename = get_latest_version_for_minor_line(resolved_target_version, resolved_major_version);
    if (!same_minor_filename.is_empty()) {
        return same_minor_filename;
    }

    String nearest_filename = get_nearest_compatible_version(resolved_target_version, resolved_major_version);
    if (!nearest_filename.is_empty()) {
        return nearest_filename;
    }

    return get_latest_version_for_godot_major(resolved_major_version);
}

String TemplateManager::get_latest_version_for_godot_major(const String& major_version) const {
    if (!versions_cache.has(major_version)) {
        return "";
    }

    Dictionary versions = versions_cache[major_version];
    Array version_keys = versions.keys();

    if (version_keys.is_empty()) {
        return "";
    }

    String latest_version = "";
    for (int i = 0; i < version_keys.size(); i++) {
        String candidate_version = String(version_keys[i]);
        if (latest_version.is_empty() || compare_version_numbers(candidate_version, latest_version) > 0) {
            latest_version = candidate_version;
        }
    }

    Dictionary normalized_entry = normalize_template_entry(versions[latest_version], "");
    String result = normalized_entry.get("filename", "");
    TOOLKIT_LOG("TemplateManager: Latest version for ", major_version, ": key=", latest_version, " -> filename=", result);
    return result;
}

bool TemplateManager::has_version(const String& godot_major, const String& version) const {
    if (!versions_cache.has(godot_major)) {
        return false;
    }

    Dictionary versions = versions_cache[godot_major];
    return versions.has(version);
}

String TemplateManager::get_template_filename(const String& godot_major, const String& version) const {
    if (!has_version(godot_major, version)) {
        return "";
    }

    Dictionary versions = versions_cache[godot_major];
    Dictionary normalized_entry = normalize_template_entry(versions[version], "");
    return normalized_entry.get("filename", "");
}

String TemplateManager::get_template_release_tag(const String& godot_major, const String& version) const {
    if (!has_version(godot_major, version)) {
        return "";
    }

    Dictionary versions = versions_cache[godot_major];
    Dictionary normalized_entry = normalize_template_entry(versions[version], "");
    return normalized_entry.get("release_tag", "");
}

bool TemplateManager::is_template_embedded(const String& filename) const {
    TOOLKIT_LOG("TemplateManager: Checking if template '", filename, "' is embedded");

#ifdef EMBED_RESOURCES
    String resource_path = "resources/templates/" + filename;
    TOOLKIT_LOG("TemplateManager: Looking for embedded resource: ", resource_path);

    // List all embedded resources for debugging
    TOOLKIT_LOG("TemplateManager: Available embedded resources:");
    for (int i = 0; toolkit::resources::embedded_resources[i].path != nullptr; i++) {
        const auto& resource = toolkit::resources::embedded_resources[i];
        TOOLKIT_LOG("  - ", String(resource.path));
        if (String(resource.path) == resource_path) {
            TOOLKIT_LOG("TemplateManager: Found embedded template: ", resource_path);
            return true;
        }
    }
    TOOLKIT_LOG("TemplateManager: Template not found in embedded resources");
#else
    TOOLKIT_LOG("TemplateManager: EMBED_RESOURCES not defined");
#endif
    return false;
}

String TemplateManager::get_bundled_template_path(const String& filename) const {
    if (filename.get_file() != filename || filename.contains("..")) {
        return "";
    }
    String path = "res://addons/godot-minigame/resources/templates/" + filename;
    return FileAccess::file_exists(path) ? path : "";
}

bool TemplateManager::is_template_bundled(const String& filename) const {
    return !get_bundled_template_path(filename).is_empty();
}

String TemplateManager::get_best_bundled_template_for_editor() const {
    String current_version = get_current_godot_version();
    String major_version = get_godot_major_version();
    if (has_version(major_version, current_version)) {
        String filename = get_template_filename(major_version, current_version);
        if (is_template_bundled(filename)) {
            return get_bundled_template_path(filename);
        }
    }

    PackedStringArray current_parts = current_version.split(".");
    String current_minor = current_parts.size() >= 2 ? String(current_parts[0]) + "." + String(current_parts[1]) + "." : "";
    String same_minor_version;
    String nearest_version;
    String latest_version;
    String same_minor_filename;
    String nearest_filename;
    String latest_filename;
    Array versions = get_available_versions();
    for (int i = 0; i < versions.size(); i++) {
        Dictionary entry = versions[i];
        if (String(entry.get("godot_major", "")) != major_version) {
            continue;
        }
        String version = String(entry.get("version", ""));
        String filename = String(entry.get("filename", ""));
        if (!is_template_bundled(filename)) {
            continue;
        }
        if (!current_minor.is_empty() && version.begins_with(current_minor) &&
                (same_minor_version.is_empty() || compare_version_numbers(version, same_minor_version) > 0)) {
            same_minor_version = version;
            same_minor_filename = filename;
        }
        if (compare_version_numbers(version, current_version) <= 0 &&
                (nearest_version.is_empty() || compare_version_numbers(version, nearest_version) > 0)) {
            nearest_version = version;
            nearest_filename = filename;
        }
        if (latest_version.is_empty() || compare_version_numbers(version, latest_version) > 0) {
            latest_version = version;
            latest_filename = filename;
        }
    }

    String selected = !same_minor_filename.is_empty() ? same_minor_filename :
            (!nearest_filename.is_empty() ? nearest_filename : latest_filename);
    if (!selected.is_empty()) {
        return get_bundled_template_path(selected);
    }
    return "";
}

bool TemplateManager::is_template_downloaded(const String& filename) const {
    String cache_path = get_download_cache_path(filename);
    if (!FileAccess::file_exists(cache_path)) {
        return false;
    }
    if (verify_template_file(filename, cache_path) && validate_template_archive(cache_path)) {
        return true;
    }
    return false;
}

String TemplateManager::get_template_path(const String& filename) const {
    // A verified global download is an explicit user override.
    String cache_path = get_download_cache_path(filename);
    if (verify_template_file(filename, cache_path) && validate_template_archive(cache_path)) {
        return cache_path;
    }

    String bundled_path = get_bundled_template_path(filename);
    if (!bundled_path.is_empty()) {
        return bundled_path;
    }

    if (is_template_embedded(filename)) {
        return "embedded://" + filename;
    }

    // Check if available remotely (but don't download automatically)
    if (is_template_available_remotely(filename)) {
        return "remote://" + filename;  // Indicates needs download
    }

    return "";
}

Error TemplateManager::download_active_template_async(bool force_replace) {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }

    const bool pending_request = !pending_template.is_empty();
    const bool custom_request = pending_request
            ? String(pending_template.get("kind", "")) == "custom"
            : active_template_kind == "custom";
    const String custom_source = pending_request
            ? String(pending_template.get("source", ""))
            : active_custom_url;
    const bool local_request = custom_request && custom_source.is_absolute_path();
    const String filename = pending_request
            ? String(pending_template.get("filename", ""))
            : (custom_request ? get_custom_template_cache_path(custom_source).get_file() : get_active_template_filename());
    const String output_path = custom_request ? get_custom_template_cache_path(custom_source) : get_download_cache_path(filename);
    if (filename.is_empty() || output_path.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }

    const bool cached_custom = custom_request && FileAccess::file_exists(output_path) && validate_template_archive(output_path);
    const bool cached_catalog = !custom_request &&
            ((is_template_bundled(filename) || is_template_embedded(filename)) ||
                    (is_template_downloaded(filename) && verify_template_file(filename, output_path) && validate_template_archive(output_path)));
    if (!force_replace && (cached_custom || cached_catalog)) {
        update_download_state(filename, "completed", 1.0f);
        if (pending_request) {
            complete_pending_template(cached_catalog && !FileAccess::file_exists(output_path)
                    ? String(pending_template.get("path", ""))
                    : output_path);
        }
        call_deferred("emit_signal", "template_download_progress", filename, 1.0f);
        call_deferred("emit_signal", "template_download_finished", filename, true);
        return OK;
    }

    if (!UserDataPath::create_directory_if_not_exists(output_path.get_base_dir())) {
        update_download_state(filename, "failed", 0.0f);
        return ERR_FILE_CANT_WRITE;
    }

    const String temporary_path = output_path + String(".replacement.") +
            String::num_int64(OS::get_singleton()->get_process_id());
    DirAccess::remove_absolute(temporary_path);

    if (local_request) {
        Error err = validate_template_archive(custom_source)
                ? DirAccess::copy_absolute(custom_source, temporary_path)
                : ERR_FILE_CORRUPT;
        if (err == OK && validate_template_archive(temporary_path)) {
            err = publish_download_atomically(temporary_path, output_path);
        }
        if (err != OK) {
            DirAccess::remove_absolute(temporary_path);
            update_download_state(filename, "failed", 0.0f);
            return err;
        }
        update_download_state(filename, "completed", 1.0f);
        complete_pending_template(output_path);
        call_deferred("emit_signal", "template_inventory_changed");
        call_deferred("emit_signal", "template_download_finished", filename, true);
        return OK;
    }

    const String download_url = custom_request ? custom_source : build_download_url(filename);
    if (download_url.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    EditorInterface *editor = EditorInterface::get_singleton();
    Node *parent_node = editor ? editor->get_editor_main_screen() : nullptr;
    if (!parent_node) {
        update_download_state(filename, "failed", 0.0f);
        return ERR_UNCONFIGURED;
    }

    HTTPRequest *request = memnew(HTTPRequest);
    request->set_name("TemplateManager_TemplateRequest");
    request->set_use_threads(false);
    request->set_accept_gzip(false);
    request->set_max_redirects(5);
    request->set_timeout(download_timeout);
    request->set_download_file(temporary_path);
    NetworkProxy::apply(request);
    parent_node->add_child(request);

    template_request_id = request->get_instance_id();
    template_request_active = true;
    template_request_custom = custom_request;
    template_request_filename = filename;
    template_request_output_path = output_path;
    template_request_temporary_path = temporary_path;

    request->connect("request_completed", callable_mp(this, &TemplateManager::_on_template_download_request_completed));

    PackedStringArray headers;
    headers.append("User-Agent: GodotMinigame/1.0");
    headers.append("Accept: application/zip, application/octet-stream, */*");
    const Error request_err = request->request(download_url, headers);
    if (request_err != OK) {
        cancel_active_template_request();
        DirAccess::remove_absolute(temporary_path);
        update_download_state(filename, "failed", 0.0f);
        return request_err;
    }

    update_download_state(filename, "downloading", 0.0f);
    emit_signal("template_download_progress", filename, 0.0f);
    return OK;
}

String TemplateManager::get_last_local_template_path() const {
    return last_local_template_path;
}

bool TemplateManager::is_prefetch_active() const {
    return template_request_active;
}

HTTPRequest *TemplateManager::get_template_request() const {
    return Object::cast_to<HTTPRequest>(ObjectDB::get_instance(uint64_t(template_request_id)));
}

void TemplateManager::release_template_request(bool cancel_request) {
    HTTPRequest *request = get_template_request();
    if (request) {
        if (cancel_request) {
            request->cancel_request();
        }
        const Callable completed_callable = callable_mp(this, &TemplateManager::_on_template_download_request_completed);
        if (request->is_connected("request_completed", completed_callable)) {
            request->disconnect("request_completed", completed_callable);
        }
        request->queue_free();
    }
    template_request_id = ObjectID();
}

void TemplateManager::cancel_active_template_request() {
    const String temporary_path = template_request_temporary_path;
    release_template_request(true);
    template_request_active = false;
    template_request_custom = false;
    template_request_filename = "";
    template_request_output_path = "";
    template_request_temporary_path = "";
    if (!temporary_path.is_empty()) {
        DirAccess::remove_absolute(temporary_path);
    }
}

void TemplateManager::_on_template_download_request_completed(
        int result,
        int response_code,
        const PackedStringArray &headers,
        const PackedByteArray &body) {
    (void)headers;
    (void)body;
    if (!template_request_active) {
        return;
    }

    const String filename = template_request_filename;
    const String output_path = template_request_output_path;
    const String temporary_path = template_request_temporary_path;
    const bool custom_request = template_request_custom;
    release_template_request(false);
    template_request_active = false;
    template_request_custom = false;
    template_request_filename = "";
    template_request_output_path = "";
    template_request_temporary_path = "";

    const String pending_sha = String(pending_template.get("sha256", "")).strip_edges().to_lower();
    const bool catalog_verified = custom_request ||
            (!pending_sha.is_empty()
                    ? FileAccess::file_exists(temporary_path) && FileAccess::get_sha256(temporary_path).to_lower() == pending_sha
                    : verify_template_file(filename, temporary_path));
    Error completion_err = OK;
    if (result != HTTPRequest::RESULT_SUCCESS || response_code != HTTPClient::RESPONSE_OK) {
        completion_err = result == HTTPRequest::RESULT_TIMEOUT ? ERR_TIMEOUT : ERR_CANT_CONNECT;
    } else if (!FileAccess::file_exists(temporary_path)) {
        completion_err = ERR_FILE_CANT_WRITE;
    } else if (!catalog_verified || !validate_template_archive(temporary_path)) {
        completion_err = ERR_FILE_CORRUPT;
    } else {
        completion_err = publish_download_atomically(temporary_path, output_path);
    }

    if (completion_err != OK) {
        DirAccess::remove_absolute(temporary_path);
        pending_template.clear();
        pending_template_activate = true;
        update_download_state(filename, "failed", 0.0f, String::utf8("错误码 ") + String::num_int64(completion_err));
        emit_signal("template_download_finished", filename, false);
        return;
    }

    update_download_state(filename, "completed", 1.0f);
    complete_pending_template(output_path);
    emit_signal("template_download_progress", filename, 1.0f);
    emit_signal("template_inventory_changed");
    emit_signal("template_download_finished", filename, true);
}

String TemplateManager::get_download_status_text(const String& filename) const {
    Dictionary info = download_states.get(filename, Dictionary());
    if (info.is_empty()) {
        return String::utf8("未下载");
    }
    String status = info.get("status", "");
    float progress = info.get("progress", 0.0f);
    String note = info.get("note", "");

    if (status == String("completed")) {
        return String::utf8("已缓存");
    }
    if (status == String("downloading")) {
        if (!note.is_empty() && note.ends_with(String("MiB"))) {
            return String::utf8("下载中，已下载 ") + note;
        }
        if (!note.is_empty()) {
            return String::utf8("下载中（") + note + String::utf8("）");
        }
        return String::utf8("下载中 ") + String::num_int64(int64_t(progress * 100.0f)) + "%";
    }
    if (status == String("failed")) {
        if (!note.is_empty()) {
            return String::utf8("下载失败（") + note + String::utf8("）");
        }
        return String::utf8("下载失败");
    }
    return String::utf8("未下载");
}

bool TemplateManager::is_downloading(const String& filename) const {
    if (!download_states.has(filename)) {
        return false;
    }

    Dictionary state = download_states[filename];
    return state.get("status", "") == "downloading";
}

float TemplateManager::get_download_progress(const String& filename) const {
    if (!download_states.has(filename)) {
        return 0.0f;
    }

    Dictionary state = download_states[filename];
    return state.get("progress", 0.0f);
}

String TemplateManager::get_current_godot_version() const {
    Dictionary version_info = Engine::get_singleton()->get_version_info();
    String major = version_info.get("major", 4);
    String minor = version_info.get("minor", 4);
    String patch = version_info.get("patch", 0);

    return major + "." + minor + "." + patch;
}

String TemplateManager::get_godot_major_version() const {
    Dictionary version_info = Engine::get_singleton()->get_version_info();
    int major = version_info.get("major", 4);
    return "godot" + String::num_int64(major);
}

String TemplateManager::format_version_string(const String& version) const {
    return "Godot " + version;
}

Error TemplateManager::extract_template(const String& template_path, const String& output_path) {
    TOOLKIT_LOG("TemplateManager: Extracting '", template_path, "' to '", output_path, "'");

    // Check if it's an embedded template
    if (template_path.begins_with("embedded://")) {
        String filename = template_path.replace("embedded://", "");
        TOOLKIT_LOG("TemplateManager: Extracting embedded template: ", filename);
        return extract_embedded_template(filename, output_path);
    }

    // For file-based templates, check if file exists and implement ZIP extraction
    if (!FileAccess::file_exists(template_path)) {
        TOOLKIT_LOG_RICH("[color=red]Template file not found: ", template_path, "[/color]");
        return ERR_FILE_NOT_FOUND;
    }

    Ref<ZIPReader> zip_reader = memnew(ZIPReader);
    Error open_result = zip_reader->open(template_path);

    if (open_result != OK) {
        TOOLKIT_LOG_RICH("[color=red]Failed to open ZIP file: ", template_path, " (error: ", open_result, ")[/color]");
        return open_result;
    }

    String absolute_output_path = _resolve_absolute_output_path(output_path);

    // Create output directory if it doesn't exist
    if (!DirAccess::dir_exists_absolute(absolute_output_path)) {
        Error mkdir_err = DirAccess::make_dir_recursive_absolute(absolute_output_path);
        if (mkdir_err != OK) {
            TOOLKIT_LOG_RICH("[color=red]Failed to create output directory: ", absolute_output_path, "[/color]");
            UtilityFunctions::push_warning(String("Template extract mkdir failed: ") + absolute_output_path);
            zip_reader->close();
            return mkdir_err;
        }
    }

    PackedStringArray files = zip_reader->get_files();
    TOOLKIT_LOG("TemplateManager: Found ", files.size(), " files in ZIP");

    for (int i = 0; i < files.size(); i++) {
        String file_path_in_zip = files[i];
        String full_dest_path = absolute_output_path.path_join(file_path_in_zip);

        if (file_path_in_zip.ends_with("/")) {
            Error mkdir_err = DirAccess::make_dir_recursive_absolute(full_dest_path);
            if (mkdir_err != OK) {
                TOOLKIT_LOG_RICH("[color=red]Failed to create directory in template extraction: ", full_dest_path, " (error: ", mkdir_err, ")[/color]");
                UtilityFunctions::push_warning(String("Template extract mkdir failed: ") + full_dest_path);
                zip_reader->close();
                return mkdir_err;
            }
        } else {
            Error mkdir_err = DirAccess::make_dir_recursive_absolute(full_dest_path.get_base_dir());
            if (mkdir_err != OK) {
                TOOLKIT_LOG_RICH("[color=red]Failed to create parent directory for file: ", full_dest_path, " (error: ", mkdir_err, ")[/color]");
                UtilityFunctions::push_warning(String("Template extract parent mkdir failed: ") + full_dest_path.get_base_dir());
                zip_reader->close();
                return mkdir_err;
            }
            PackedByteArray data = zip_reader->read_file(file_path_in_zip);
            Ref<FileAccess> file = FileAccess::open(full_dest_path, FileAccess::WRITE);
            if (file.is_valid()) {
                file->store_buffer(data);
                file->close();
            } else {
                TOOLKIT_LOG_RICH("[color=red]Failed to write file: ", full_dest_path, "[/color]");
                UtilityFunctions::push_warning(String("Template extract write failed: ") + full_dest_path);
                zip_reader->close();
                return ERR_FILE_CANT_WRITE;
            }
        }
    }

    zip_reader->close();
    TOOLKIT_LOG_RICH("[color=green]Successfully extracted template '", template_path, "' to '", absolute_output_path, "'[/color]");
    return OK;
}

PackedStringArray TemplateManager::get_template_entries(const String& template_path) const {
    PackedStringArray entries;

    if (template_path.begins_with("embedded://")) {
#ifdef EMBED_RESOURCES
        const String filename = template_path.replace("embedded://", "");
        const String resource_path = "resources/templates/" + filename;
        const toolkit::resources::EmbeddedResourceEntry* resource = nullptr;
        for (int i = 0; toolkit::resources::embedded_resources[i].path != nullptr; i++) {
            if (String(toolkit::resources::embedded_resources[i].path) == resource_path) {
                resource = &toolkit::resources::embedded_resources[i];
                break;
            }
        }
        if (resource != nullptr) {
            // ZIPReader 只能从文件路径打开,把 embedded 字节落到临时文件再枚举
            const String temp_zip_path = "user://.godot-minigame-template-entries.tmp";
            Ref<FileAccess> temp_file = FileAccess::open(temp_zip_path, FileAccess::WRITE);
            if (temp_file.is_valid()) {
                PackedByteArray data;
                data.resize(resource->size);
                memcpy(data.ptrw(), resource->data, resource->size);
                temp_file->store_buffer(data);
                temp_file->close();

                Ref<ZIPReader> entry_zip_reader = memnew(ZIPReader);
                if (entry_zip_reader->open(temp_zip_path) == OK) {
                    entries = entry_zip_reader->get_files();
                    entry_zip_reader->close();
                }
                DirAccess::remove_absolute(temp_zip_path);
            }
        }
#endif
        return entries;
    }

    if (!FileAccess::file_exists(template_path)) {
        return entries;
    }

    Ref<ZIPReader> zip_reader = memnew(ZIPReader);
    if (zip_reader->open(template_path) != OK) {
        return entries;
    }
    entries = zip_reader->get_files();
    zip_reader->close();
    return entries;
}

Error TemplateManager::extract_embedded_template(const String& filename, const String& output_path) {
    TOOLKIT_LOG("TemplateManager: Extracting embedded template '", filename, "' to '", output_path, "'");

#ifdef EMBED_RESOURCES
    String resource_path = "resources/templates/" + filename;
    for (int i = 0; toolkit::resources::embedded_resources[i].path != nullptr; i++) {
        const auto& resource = toolkit::resources::embedded_resources[i];
        if (String(resource.path) == resource_path) {
            String absolute_output_path = _resolve_absolute_output_path(output_path);

            TOOLKIT_LOG("TemplateManager: Using absolute output path: ", absolute_output_path);

            String temp_zip_path = absolute_output_path.path_join(String("temp_") + filename);
            String extract_dir = absolute_output_path; // Extract directly to output path, no subfolder

            // Create the output directory if it doesn't exist
            if (!DirAccess::dir_exists_absolute(absolute_output_path)) {
                Error mkdir_result = DirAccess::make_dir_recursive_absolute(absolute_output_path);
                if (mkdir_result != OK) {
                    TOOLKIT_LOG_RICH("[color=red]Cannot create output directory: ", absolute_output_path, " (error: ", mkdir_result, ")[/color]");
                    UtilityFunctions::push_warning(String("Embedded template mkdir failed: ") + absolute_output_path);
                    return mkdir_result;
                }
                TOOLKIT_LOG("TemplateManager: Created directory: ", absolute_output_path);
            }

            // Write embedded ZIP to temp file
            Ref<FileAccess> temp_file = FileAccess::open(temp_zip_path, FileAccess::WRITE);
            if (temp_file.is_null()) {
                TOOLKIT_LOG_RICH("[color=red]Cannot create temp file: ", temp_zip_path, "[/color]");
                UtilityFunctions::push_warning(String("Embedded template temp write failed: ") + temp_zip_path);
                return ERR_FILE_CANT_WRITE;
            }

            PackedByteArray data;
            data.resize(resource.size);
            memcpy(data.ptrw(), resource.data, resource.size);
            temp_file->store_buffer(data);
            temp_file->close();

            // Extract the ZIP file using Godot's ZIPReader
            Ref<ZIPReader> zip_reader = memnew(ZIPReader);
            Error open_result = zip_reader->open(temp_zip_path);

            if (open_result == OK) {
                PackedStringArray files = zip_reader->get_files();
                TOOLKIT_LOG("TemplateManager: Found ", files.size(), " files in ZIP");

                for (int f = 0; f < files.size(); f++) {
                    String file_path = files[f];
                    String output_file_path = extract_dir + "/" + file_path;

                    TOOLKIT_LOG("Extracting: ", file_path, " -> ", output_file_path);

                    // Skip directory entries (they end with "/")
                    if (file_path.ends_with("/")) {
                        TOOLKIT_LOG("  Skipping directory entry: ", file_path);
                        continue;
                    }

                    // Create directory structure for this file
                    String file_dir = output_file_path.get_base_dir();
                    if (!DirAccess::dir_exists_absolute(file_dir)) {
                        Error mkdir_result = DirAccess::make_dir_recursive_absolute(file_dir);
                        if (mkdir_result != OK) {
                            TOOLKIT_LOG_RICH("[color=red]Failed to create directory: ", file_dir, " (error: ", mkdir_result, ")[/color]");
                            UtilityFunctions::push_warning(String("Embedded template mkdir failed: ") + file_dir);
                            zip_reader->close();
                            DirAccess::remove_absolute(temp_zip_path);
                            return mkdir_result;
                        }
                    }

                    // Read file data from ZIP
                    PackedByteArray file_data = zip_reader->read_file(file_path);
                    if (file_data.size() > 0) {
                        // Write file to disk
                        Ref<FileAccess> output_file = FileAccess::open(output_file_path, FileAccess::WRITE);
                        if (output_file.is_valid()) {
                            output_file->store_buffer(file_data);
                            output_file->close();
                            TOOLKIT_LOG("  Written: ", file_data.size(), " bytes");
                        } else {
                            TOOLKIT_LOG_RICH("[color=red]  Failed to write: ", output_file_path, "[/color]");
                            UtilityFunctions::push_warning(String("Embedded template write failed: ") + output_file_path);
                            zip_reader->close();
                            DirAccess::remove_absolute(temp_zip_path);
                            return ERR_FILE_CANT_WRITE;
                        }
                    } else if (!file_path.ends_with("/")) {
                        TOOLKIT_LOG("  Empty file: ", file_path);
                        // Create empty file
                        Ref<FileAccess> empty_file = FileAccess::open(output_file_path, FileAccess::WRITE);
                        if (empty_file.is_valid()) {
                            empty_file->close();
                        } else {
                            TOOLKIT_LOG_RICH("[color=red]  Failed to create empty file: ", output_file_path, "[/color]");
                            UtilityFunctions::push_warning(String("Embedded template empty file create failed: ") + output_file_path);
                            zip_reader->close();
                            DirAccess::remove_absolute(temp_zip_path);
                            return ERR_FILE_CANT_WRITE;
                        }
                    }
                }

                zip_reader->close();
                TOOLKIT_LOG_RICH("[color=green]Successfully extracted ", files.size(), " files from ZIP[/color]");
            } else {
                TOOLKIT_LOG_RICH("[color=red]Failed to open ZIP file: ", temp_zip_path, " (error: ", open_result, ")[/color]");
            }

            // Clean up temp file
            DirAccess::remove_absolute(temp_zip_path);

            TOOLKIT_LOG_RICH("[color=green]Successfully extracted embedded template '", filename, "' to '", extract_dir, "'[/color]");
            return OK;
        }
    }
    TOOLKIT_LOG_RICH("[color=red]Embedded template not found: ", resource_path, "[/color]");
#else
    TOOLKIT_LOG_RICH("[color=red]EMBED_RESOURCES not defined[/color]");
#endif
    return ERR_FILE_NOT_FOUND;
}

Error TemplateManager::remove_active_template_cache() {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }
    String path;
    if (active_template_kind == "custom") {
        path = active_custom_url.is_empty() ? String() : get_custom_template_cache_path(active_custom_url);
    } else {
        const String filename = get_active_template_filename();
        path = filename.is_empty() ? String() : get_download_cache_path(filename);
    }
    if (path.is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    Error err = FileAccess::file_exists(path) ? DirAccess::remove_absolute(path) : OK;
    if (err == OK) {
        download_states.erase(path.get_file());
        emit_signal("template_inventory_changed");
        emit_signal("active_template_changed", get_active_template_info());
    }
    return err;
}

Error TemplateManager::clear_all_template_cache() {
    if (is_prefetch_active()) {
        return ERR_BUSY;
    }
    const String root = get_global_template_cache_root();
    Error err = _remove_directory_recursive_absolute(root);
    if (err != OK) {
        return err;
    }
    DirAccess::make_dir_recursive_absolute(root);
    download_states.clear();
    emit_signal("template_inventory_changed");
    emit_signal("active_template_changed", get_active_template_info());
    return OK;
}

Error TemplateManager::refresh_versions() {
    return load_versions_from_remote();
}

void TemplateManager::set_download_timeout(int timeout_seconds) {
    download_timeout = timeout_seconds;
}

int TemplateManager::get_download_timeout() const {
    return download_timeout;
}

String TemplateManager::build_versions_url() const {
    return pending_catalog_url.is_empty() ? remote_catalog_url : pending_catalog_url;
}

String TemplateManager::build_download_url(const String& filename) const {
    for (int i = 0; i < available_versions.size(); i++) {
        const Dictionary entry = available_versions[i];
        if (String(entry.get("filename", "")) == filename) {
            return String(entry.get("download_url", "")).strip_edges();
        }
    }
    return "";
}

String TemplateManager::get_download_cache_path(const String& filename) const {
    String templates_dir = get_global_template_cache_root().path_join("artifacts");
    DirAccess::make_dir_recursive_absolute(templates_dir);
    const String expected_sha = find_sha256_for_filename(filename);
    return templates_dir.path_join(expected_sha.is_empty() ? filename : expected_sha + String(".tpz"));
}

String TemplateManager::get_local_versions_cache_path() const {
    return get_global_template_cache_root().path_join("catalog.json");
}

void TemplateManager::update_download_state(const String& filename, const String& state, float progress, const String& note) {
    Dictionary download_info;
    download_info["status"] = state;
    download_info["progress"] = progress;
    download_info["timestamp"] = Time::get_singleton()->get_unix_time_from_system();
    if (!note.is_empty()) {
        download_info["note"] = note;
    }

    download_states[filename] = download_info;
}

// New helper methods for template availability checking
bool TemplateManager::is_template_available_remotely(const String& filename) const {
    // Check if template exists in versions configuration
    Array versions = get_available_versions();
    for (int i = 0; i < versions.size(); i++) {
        Dictionary version_info = versions[i];
        if (version_info.get("filename", "") == filename) {
            return true;
        }
    }
    return false;
}

bool TemplateManager::is_template_available_anywhere(const String& filename) const {
    return is_template_bundled(filename) ||
           is_template_embedded(filename) ||
           is_template_downloaded(filename) ||
           is_template_available_remotely(filename);
}

String TemplateManager::get_best_available_template_for_editor() const {
    String current_version = get_current_godot_version();
    String major_version = get_godot_major_version();

    // Get the best template filename first
    String best_filename = get_best_version_for_editor();
    if (best_filename.is_empty()) {
        TOOLKIT_LOG("TemplateManager: No best filename resolved for editor version ", current_version, " (", major_version, ")");
        return "";
    }

    // Check availability with priority: embedded -> cached -> remote
    String template_path = get_template_path(best_filename);
    TOOLKIT_LOG("TemplateManager: Best template for editor version ", current_version, " (", major_version, ") is filename=", best_filename, ", path=", template_path);
    return template_path;
}

String TemplateManager::get_best_available_template_for_version(const String &target_version, const String &major_version) const {
    String best_filename = resolve_template_filename_for_version(target_version, major_version);
    if (best_filename.is_empty()) {
        return "";
    }
    return get_template_path(best_filename);
}

Error TemplateManager::save_versions_to_local_cache() {
    if (!versions_loaded || catalog_cache.is_empty()) {
        return ERR_INVALID_DATA;
    }

    String local_cache_path = get_local_versions_cache_path();

    // Ensure directory exists
    String cache_dir = local_cache_path.get_base_dir();
    UserDataPath::create_directory_if_not_exists(cache_dir);

    String temporary_path = local_cache_path + ".tmp";
    Ref<FileAccess> file = FileAccess::open(temporary_path, FileAccess::WRITE);
    if (file.is_null()) {
        return ERR_FILE_CANT_WRITE;
    }

    file->store_string(JSON::stringify(catalog_cache, "  ") + "\n");
    file->close();

    if (FileAccess::file_exists(local_cache_path)) {
        DirAccess::remove_absolute(local_cache_path);
    }
    Error rename_err = DirAccess::rename_absolute(temporary_path, local_cache_path);
    if (rename_err != OK) {
        DirAccess::remove_absolute(temporary_path);
        return rename_err;
    }

    TOOLKIT_LOG("TemplateManager: Saved templates catalog to local cache: ", local_cache_path);
    return OK;
}

// Version comparison and nearest matching implementation
String TemplateManager::get_nearest_compatible_version(const String& target_version, const String& major_version) const {
    TOOLKIT_LOG("TemplateManager: Looking for nearest compatible version for ", target_version, " in ", major_version);

    if (!versions_cache.has(major_version)) {
        TOOLKIT_LOG("TemplateManager: Major version ", major_version, " not found in cache");
        return "";
    }

    Dictionary versions = versions_cache[major_version];
    Array version_keys = versions.keys();

    TOOLKIT_LOG("TemplateManager: Available versions: ", version_keys);

    if (version_keys.is_empty()) {
        return "";
    }

    String best_match = "";
    String best_filename = "";
    int best_diff = INT_MAX;

    for (int i = 0; i < version_keys.size(); i++) {
        Variant candidate_key = version_keys[i];
        String candidate_version = candidate_key;

        // Handle both numeric and string keys from YAML parsing
        Variant entry_variant = versions.has(candidate_key) ? versions[candidate_key] : versions[candidate_version];
        Dictionary normalized_entry = normalize_template_entry(entry_variant, "");
        String candidate_filename = normalized_entry.get("filename", "");

        TOOLKIT_LOG("TemplateManager: Checking candidate ", candidate_version, " -> ", candidate_filename, " (key: ", candidate_key, " type: ", candidate_key.get_type(), ")");

        if (candidate_filename.is_empty() || candidate_filename == "<null>") {
            TOOLKIT_LOG("TemplateManager: Skipping candidate with invalid filename");
            continue;
        }

        // Only consider versions that are <= target version (no future versions)
        int comparison = compare_version_numbers(candidate_version, target_version);
        TOOLKIT_LOG("TemplateManager: Version comparison ", candidate_version, " vs ", target_version, " = ", comparison);

        if (comparison > 0) {
            TOOLKIT_LOG("TemplateManager: Skipping newer version ", candidate_version);
            continue;  // Skip versions newer than target
        }

        // Calculate "distance" from target version (closer to 0 is better)
        int diff = abs(comparison);
        TOOLKIT_LOG("TemplateManager: Distance = ", diff, ", best_diff = ", best_diff);

        if (diff < best_diff) {
            best_diff = diff;
            best_match = candidate_version;
            best_filename = candidate_filename;
            TOOLKIT_LOG("TemplateManager: New best match: ", best_match, " -> ", best_filename);
        } else if (diff == best_diff) {
            // If distances are equal, prefer the newer version
            int version_comparison = compare_version_numbers(candidate_version, best_match);
            if (version_comparison > 0) {
                best_match = candidate_version;
                best_filename = candidate_filename;
                TOOLKIT_LOG("TemplateManager: Equal distance, preferring newer version: ", best_match, " -> ", best_filename);
            }
        }
    }

    TOOLKIT_LOG("TemplateManager: Final best match: '", best_match, "' -> '", best_filename, "'");
    return best_filename;
}

String TemplateManager::get_latest_version_for_minor_line(const String& target_version, const String& major_version) const {
    if (!versions_cache.has(major_version)) {
        return "";
    }

    PackedStringArray target_parts = target_version.split(".");
    if (target_parts.size() < 2) {
        return "";
    }

    String target_major = String(target_parts[0]).strip_edges();
    String target_minor = String(target_parts[1]).strip_edges();
    if (target_major.is_empty() || target_minor.is_empty()) {
        return "";
    }

    Dictionary versions = versions_cache[major_version];
    Array version_keys = versions.keys();

    String best_version = "";
    String best_filename = "";

    for (int i = 0; i < version_keys.size(); i++) {
        Variant candidate_key = version_keys[i];
        String candidate_version = String(candidate_key).strip_edges();
        PackedStringArray candidate_parts = candidate_version.split(".");
        if (candidate_parts.size() < 2) {
            continue;
        }

        if (String(candidate_parts[0]).strip_edges() != target_major ||
                String(candidate_parts[1]).strip_edges() != target_minor) {
            continue;
        }

        Variant entry_variant = versions.has(candidate_key) ? versions[candidate_key] : versions[candidate_version];
        Dictionary normalized_entry = normalize_template_entry(entry_variant, "");
        String candidate_filename = String(normalized_entry.get("filename", "")).strip_edges();
        if (candidate_filename.is_empty() || candidate_filename == "<null>") {
            continue;
        }

        if (best_version.is_empty() || compare_version_numbers(candidate_version, best_version) > 0) {
            best_version = candidate_version;
            best_filename = candidate_filename;
        }
    }

    return best_filename;
}

Array TemplateManager::get_compatible_versions_for_major(const String& major_version) const {
    Array compatible_versions;

    if (!versions_cache.has(major_version)) {
        return compatible_versions;
    }

    Dictionary versions = versions_cache[major_version];
    Array version_keys = versions.keys();

    for (int i = 0; i < version_keys.size(); i++) {
        String version = version_keys[i];
        Dictionary normalized_entry = normalize_template_entry(versions[version], "");
        String filename = normalized_entry.get("filename", "");
        String release_tag = normalized_entry.get("release_tag", "");

        Dictionary version_info;
        version_info["version"] = version;
        version_info["filename"] = filename;
        version_info["release_tag"] = release_tag;
        version_info["major_version"] = major_version;
        version_info["is_embedded"] = is_template_embedded(filename);
        version_info["is_downloaded"] = is_template_downloaded(filename);
        version_info["is_available_remotely"] = is_template_available_remotely(filename);

        compatible_versions.append(version_info);
    }

    return compatible_versions;
}

int TemplateManager::compare_version_numbers(const String& version1, const String& version2) const {
    Array components1 = parse_version_components(version1);
    Array components2 = parse_version_components(version2);

    TOOLKIT_LOG("TemplateManager: Comparing '", version1, "' ", components1, " vs '", version2, "' ", components2);

    int max_length = Math::max(components1.size(), components2.size());

    for (int i = 0; i < max_length; i++) {
        int comp1 = (i < components1.size()) ? int(components1[i]) : 0;
        int comp2 = (i < components2.size()) ? int(components2[i]) : 0;

        if (comp1 < comp2) {
            TOOLKIT_LOG("TemplateManager: '", version1, "' < '", version2, "' (component ", i, ": ", comp1, " < ", comp2, ")");
            return -1;
        }
        if (comp1 > comp2) {
            TOOLKIT_LOG("TemplateManager: '", version1, "' > '", version2, "' (component ", i, ": ", comp1, " > ", comp2, ")");
            return 1;
        }
    }

    TOOLKIT_LOG("TemplateManager: '", version1, "' == '", version2, "' (equal)");
    return 0;  // Equal
}

Array TemplateManager::parse_version_components(const String& version) const {
    Array components;
    PackedStringArray parts = version.split(".");

    for (int i = 0; i < parts.size(); i++) {
        String part = parts[i];
        // Extract numeric part only (ignore suffixes like "beta", "rc", etc.)
        int numeric_part = part.to_int();
        components.append(numeric_part);
    }

    return components;
}

Array TemplateManager::build_available_versions_from_cache() const {
    Array rebuilt_versions;

    Dictionary dict = versions_cache;
    Array godot_majors = dict.keys();

    for (int i = 0; i < godot_majors.size(); i++) {
        String major = String(godot_majors[i]);
        Variant versions_variant = dict[godot_majors[i]];
        if (versions_variant.get_type() != Variant::DICTIONARY) {
            continue;
        }

        Dictionary versions = versions_variant;
        Array version_keys = versions.keys();
        for (int j = 0; j < version_keys.size(); j++) {
            Variant version_key = version_keys[j];
            String version = String(version_key);

            Variant entry_variant = versions.has(version_key) ? versions[version_key] : versions[version];
            Dictionary normalized_entry = normalize_template_entry(entry_variant, "");
            String filename = String(normalized_entry.get("filename", "")).strip_edges();
            if (filename.is_empty() || filename == "<null>") {
                continue;
            }

            Dictionary version_info;
            version_info["godot_major"] = major;
            version_info["version"] = version;
            version_info["filename"] = filename;
            version_info["release_tag"] = String(normalized_entry.get("release_tag", "")).strip_edges();
            version_info["sha256"] = String(normalized_entry.get("sha256", "")).strip_edges();
            version_info["download_url"] = String(normalized_entry.get("download_url", "")).strip_edges();
            version_info["is_embedded"] = is_template_embedded(filename);

            rebuilt_versions.append(version_info);
        }
    }

    return rebuilt_versions;
}

Error TemplateManager::initialize_template_system() {
    TOOLKIT_LOG("TemplateManager: Initializing template system...");
    load_active_template_selection();

    // Step 1: Try to load from local cache first
    Error load_result = load_versions_from_local_cache();

    if (load_result != OK) {
        TOOLKIT_LOG("TemplateManager: Local cache not available, trying embedded versions");
        // Step 2: Fall back to embedded versions
        load_result = load_versions_from_embedded();

        if (load_result != OK) {
            TOOLKIT_LOG_RICH("[color=yellow]Warning: No version data available[/color]");
            return load_result;
        }
    }

    migrate_legacy_template_cache();
    ensure_default_active_template();

    // Step 3: Check template status for current editor
    String template_status = check_editor_template_status();
    TOOLKIT_LOG("TemplateManager: Current editor template status: ", template_status);

    return OK;
}

bool TemplateManager::validate_template_archive(const String &path) const {
    if (!FileAccess::file_exists(path)) {
        return false;
    }
    Ref<ZIPReader> reader;
    reader.instantiate();
    if (reader->open(path) != OK) {
        return false;
    }
    PackedStringArray files = reader->get_files();
    bool has_game_json = false;
    bool has_engine = false;
    for (int i = 0; i < files.size(); i++) {
        const String entry = String(files[i]).replace("\\", "/");
        if (entry == "game.json" || entry.ends_with("/game.json")) {
            has_game_json = true;
        }
        if (entry.begins_with("engine/") || entry.contains("/engine/")) {
            has_engine = true;
        }
    }
    reader->close();
    return has_game_json && has_engine;
}

Error TemplateManager::publish_download_atomically(const String &temporary_path, const String &output_path) const {
    const String backup_path = output_path + String(".backup");
    DirAccess::remove_absolute(backup_path);
    const bool had_previous = FileAccess::file_exists(output_path);
    if (had_previous) {
        Error backup_err = DirAccess::rename_absolute(output_path, backup_path);
        if (backup_err != OK) {
            return backup_err;
        }
    }
    Error publish_err = DirAccess::rename_absolute(temporary_path, output_path);
    if (publish_err != OK) {
        if (had_previous) {
            DirAccess::rename_absolute(backup_path, output_path);
        }
        return publish_err;
    }
    if (had_previous) {
        DirAccess::remove_absolute(backup_path);
    }
    return OK;
}

void TemplateManager::load_active_template_selection() {
    const bool has_state = PluginStateStore::has_section(TEMPLATE_STATE_SECTION);
    Dictionary template_state = PluginStateStore::load_section(TEMPLATE_STATE_SECTION);
    if (has_state) {
        last_local_template_path = String(template_state.get("last_local_path", ""));
        if (!last_local_template_path.is_empty()) {
            last_local_template_path = last_local_template_path.simplify_path();
        }
        const String legacy_channel = String(template_state.get("last_channel", "remote")).strip_edges().to_lower();
        template_view = String(template_state.get("view", legacy_channel == "local" ? String("local") : String("remote"))).strip_edges().to_lower();
        if (template_view != "remote" && template_view != "local") {
            template_view = "remote";
        }
        remote_catalog_url = String(template_state.get("remote_catalog_url", DEFAULT_TEMPLATE_CATALOG_URL)).strip_edges();
        if (!remote_catalog_url.begins_with("https://") && !remote_catalog_url.begins_with("http://")) {
            remote_catalog_url = DEFAULT_TEMPLATE_CATALOG_URL;
        }
        local_template_records = template_state.get("local_artifacts", Dictionary());
        Dictionary active_by_editor = template_state.get("active_by_editor", Dictionary());
        Dictionary active = active_by_editor.get(get_editor_version_line(), Dictionary());
        active_template_id = String(active.get("artifact_id", "")).strip_edges();
        active_template_path = String(active.get("path", ""));
        if (!active_template_path.is_empty() && !active_template_path.begins_with("embedded://")) {
            active_template_path = active_template_path.simplify_path();
        }
        active_template_display_name = String(active.get("display_name", "")).strip_edges();
        active_template_origin = String(active.get("origin", "")).strip_edges();
        active_template_kind = String(active.get("kind", active_template_kind)).strip_edges().to_lower();
        active_template_version = String(active.get("version", active_template_version)).strip_edges();
        active_custom_url = String(active.get("custom_source", active_custom_url)).strip_edges();
        if (!active_template_id.is_empty()) {
            active_template_kind = "local";
        } else if (active_template_kind != "custom") {
            active_template_kind = "catalog";
        }
        return;
    }

    EditorInterface *editor_interface = EditorInterface::get_singleton();
    Ref<EditorSettings> editor_settings = editor_interface ? editor_interface->get_editor_settings() : Ref<EditorSettings>();
    if (editor_settings.is_valid()) {
        const String prefix = String(EDITOR_SETTING_PREFIX) + "active/" + get_editor_version_line() + "/";
        if (editor_settings->has_setting(prefix + String("kind"))) {
            active_template_kind = String(editor_settings->get_setting(prefix + String("kind")));
        }
        if (editor_settings->has_setting(prefix + String("version"))) {
            active_template_version = String(editor_settings->get_setting(prefix + String("version")));
        }
        if (editor_settings->has_setting(prefix + String("custom_url"))) {
            active_custom_url = String(editor_settings->get_setting(prefix + String("custom_url")));
        }
    }
    if (active_custom_url.is_absolute_path()) {
        last_local_template_path = active_custom_url.simplify_path();
    }
    persist_active_template_selection();
}

void TemplateManager::persist_active_template_selection() const {
    Dictionary template_state = PluginStateStore::load_section(TEMPLATE_STATE_SECTION);
    Dictionary active_by_editor = template_state.get("active_by_editor", Dictionary());
    Dictionary active;
    active["kind"] = active_template_kind;
    active["version"] = active_template_version;
    active["custom_source"] = active_custom_url;
    active["artifact_id"] = active_template_id;
    active["path"] = active_template_path;
    active["display_name"] = active_template_display_name;
    active["origin"] = active_template_origin;
    active_by_editor[get_editor_version_line()] = active;
    template_state["last_local_path"] = last_local_template_path;
    template_state["view"] = template_view;
    template_state["remote_catalog_url"] = remote_catalog_url;
    template_state.erase("last_channel");
    template_state["local_artifacts"] = local_template_records;
    template_state["active_by_editor"] = active_by_editor;
    PluginStateStore::save_section(TEMPLATE_STATE_SECTION, template_state);
}

void TemplateManager::ensure_default_active_template() {
    if (!active_template_id.is_empty() && !resolve_active_template_path().is_empty()) {
        return;
    }
    if (!active_template_id.is_empty()) {
        active_template_id = "";
        active_template_path = "";
        active_template_display_name = "";
        active_template_origin = "";
    }

    if (active_template_kind == "custom" && !active_custom_url.is_empty()) {
        const String custom_path = get_custom_template_cache_path(active_custom_url);
        if (FileAccess::file_exists(custom_path) && validate_template_archive(custom_path)) {
            Dictionary migrated;
            migrated["id"] = "import:" + active_custom_url.md5_text();
            migrated["display_name"] = active_custom_url.get_slice("?", 0).get_file();
            migrated["origin"] = active_custom_url.is_absolute_path() ? String::utf8("用户导入") : String::utf8("网址导入");
            migrated["provider"] = "local";
            migrated["source"] = active_custom_url;
            migrated["version"] = "";
            activate_local_template(migrated, custom_path);
            return;
        }
    }

    Array choices = get_remote_template_choices();
    for (int i = 0; i < choices.size(); i++) {
        Dictionary choice = choices[i];
        if (String(choice.get("version", "")) == active_template_version && bool(choice.get("available", false))) {
            activate_local_template(choice, String(choice.get("path", "")));
            return;
        }
    }

    Dictionary best_available;
    for (int i = 0; i < choices.size(); i++) {
        Dictionary choice = choices[i];
        if (!bool(choice.get("available", false))) {
            continue;
        }
        if (best_available.is_empty() || compare_version_numbers(
                String(choice.get("version", "")),
                String(best_available.get("version", ""))) > 0) {
            best_available = choice;
        }
    }
    if (!best_available.is_empty()) {
        activate_local_template(best_available, String(best_available.get("path", "")));
        return;
    }

    active_template_kind = "local";
    active_template_version = "";
    active_custom_url = "";
    persist_active_template_selection();
}

String TemplateManager::get_global_template_cache_root() const {
    const String root = PluginStateStore::get_root_dir().path_join("templates");
    DirAccess::make_dir_recursive_absolute(root);
    return root;
}

bool TemplateManager::has_template_updates_available() const {
    if (!versions_loaded) {
        return false;
    }

    // Check if there are newer templates available that we don't have locally
    Array missing = get_missing_templates();
    return !missing.is_empty();
}

Array TemplateManager::get_missing_templates() const {
    Array missing_templates;

    if (!versions_loaded) {
        return missing_templates;
    }

    // Check each available version to see if we have it locally (downloaded or embedded)
    for (int i = 0; i < available_versions.size(); i++) {
        Dictionary version_info = available_versions[i];
        String filename = version_info.get("filename", "");

        if (!filename.is_empty()) {
            bool is_available = is_template_embedded(filename) || is_template_downloaded(filename);
            if (!is_available) {
                missing_templates.append(version_info);
            }
        }
    }

    return missing_templates;
}

String TemplateManager::check_editor_template_status() const {
    if (!versions_loaded) {
        return "not_initialized";
    }

    String current_version = get_current_godot_version();
    String major_version = get_godot_major_version();

    // Check for exact version match
    if (has_version(major_version, current_version)) {
        String exact_template = get_template_filename(major_version, current_version);
        if (is_template_embedded(exact_template) || is_template_downloaded(exact_template)) {
            return "exact_match_available";
        } else {
            return "exact_match_missing";  // Template exists in versions but not downloaded/embedded
        }
    }

    // Check for compatible version (latest in same major version)
    String latest_template = get_latest_version_for_godot_major(major_version);
    if (!latest_template.is_empty()) {
        if (is_template_embedded(latest_template) || is_template_downloaded(latest_template)) {
            return "compatible_available";
        } else {
            return "compatible_missing";
        }
    }

    return "no_compatible_version";
}

void TemplateManager::_on_versions_download_completed(int p_result, int p_response_code, const PackedStringArray& p_headers, const PackedByteArray& p_body, const String& request_url) {
    refresh_in_flight = false;
    TOOLKIT_LOG("TemplateManager: templates catalog download completed. Result: ", p_result, ", Response Code: ", p_response_code);

    // The HTTPRequest node is a child of the editor main screen, it will be freed automatically.

    if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code == 200) {
        String catalog_content = p_body.get_string_from_utf8();
        Error parse_err = parse_templates_catalog(catalog_content);
        if (parse_err == OK) {
            remote_catalog_url = request_url;
            pending_catalog_url = "";
            persist_active_template_selection();
            TOOLKIT_LOG("TemplateManager: Successfully parsed remote templates catalog");
            save_versions_to_local_cache();
            ensure_default_active_template();
            emit_signal("versions_loaded");
            emit_signal("template_inventory_changed");
        } else {
            pending_catalog_url = "";
            TOOLKIT_LOG_RICH("[color=red]TemplateManager: Failed to parse remote templates catalog; retaining current catalog.[/color]");
            emit_signal("versions_refresh_failed", parse_err);
        }
    } else {
        pending_catalog_url = "";
        TOOLKIT_LOG_RICH("[color=red]TemplateManager: Failed to download templates catalog; retaining current catalog.[/color]");
        emit_signal("versions_refresh_failed", ERR_CANT_CONNECT);
    }
}

} // namespace templates
} // namespace toolkit
