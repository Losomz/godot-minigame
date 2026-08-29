#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <functional>

using namespace godot;

namespace toolkit {
namespace templates {

// Template version information
struct TemplateVersion {
    String godot_major;     // "godot3" or "godot4"
    String version;         // "4.4.0", "4.3.0", etc.
    String filename;        // "minigame4.4.0.1.tpz"
    String release_tag;     // Release tag that actually contains the asset
    bool is_embedded;       // true if bundled in DLL
};

// Template manager for version handling and downloads
class TemplateManager : public Object {
    GDCLASS(TemplateManager, Object);

private:
    static TemplateManager* singleton;

    Dictionary versions_cache;
    Dictionary catalog_cache;
    Array available_versions;
    bool versions_loaded = false;
    bool refresh_in_flight = false;
    int64_t catalog_revision = 0;

protected:
    static void _bind_methods();

public:
    TemplateManager();
    ~TemplateManager();

    static TemplateManager* get_singleton();

    // Version management
    Error load_versions_from_remote();
    Error load_versions_from_remote_sync();
    Error load_versions_from_embedded();
    Error load_versions_from_local_cache();
    Array get_available_versions() const;
    Dictionary get_versions_data() const;
    int64_t get_catalog_revision() const;

    // Single editor-global template selection used by every project export.
    Array get_remote_template_choices() const;
    Array get_local_template_choices() const;
    Dictionary get_active_template_info() const;
    Error cache_template(const String& template_id);
    Error set_current_template(const String& template_id);
    Error import_local_template(const String& path);
    Error refresh_remote_catalog(const String& catalog_url);
    void set_template_view(const String& view);
    String get_template_view() const;
    String get_remote_catalog_url() const;
    String get_last_local_template_path() const;
    String resolve_active_template_path() const;

    // Version selection with nearest match
    String get_best_version_for_editor() const;
    String resolve_template_filename_for_version(const String& target_version, const String& major_version = "") const;
    String get_nearest_compatible_version(const String& target_version, const String& major_version) const;
    String get_latest_version_for_godot_major(const String& major_version) const;
    bool has_version(const String& godot_major, const String& version) const;
    String get_template_filename(const String& godot_major, const String& version) const;
    String get_template_release_tag(const String& godot_major, const String& version) const;
    Array get_compatible_versions_for_major(const String& major_version) const;

    // Template availability (priority: embedded -> cached -> remote)
    bool is_template_embedded(const String& filename) const;
    bool is_template_bundled(const String& filename) const;
    bool is_template_downloaded(const String& filename) const;
    bool is_template_available_remotely(const String& filename) const;
    String get_template_path(const String& filename) const;
    String get_bundled_template_path(const String& filename) const;
    String get_best_bundled_template_for_editor() const;
    String get_best_available_template_for_editor() const;
    String get_best_available_template_for_version(const String& target_version, const String& major_version = "") const;

    // Update detection
    bool has_template_updates_available() const;
    Array get_missing_templates() const;
    String check_editor_template_status() const;

    // Download status
    bool is_downloading(const String& filename) const;
    float get_download_progress(const String& filename) const;

    // Editor-side template download (manual trigger, driven by main-thread nodes)
    Error download_active_template_async(bool force_replace = false);
    bool is_prefetch_active() const;
    String get_download_status_text(const String& filename) const;

    // Utility functions
    String get_current_godot_version() const;
    String get_godot_major_version() const;
    String format_version_string(const String& version) const;

    // Template extraction
    Error extract_template(const String& template_path, const String& output_path);
    Error extract_embedded_template(const String& filename, const String& output_path);

    // Cache management
    Error remove_active_template_cache();
    Error clear_all_template_cache();
    Error refresh_versions();
    Error initialize_template_system();

    void set_download_timeout(int timeout_seconds);
    int get_download_timeout() const;

private:
    Dictionary download_states;
    int download_timeout = 30;

    // Active remote template request.
    ObjectID template_request_id;
    bool template_request_active = false;
    bool template_request_custom = false;
    String template_request_filename;
    String template_request_output_path;
    String template_request_temporary_path;

    String active_template_kind = "catalog";
    String active_template_version;
    String active_custom_url;
    String last_local_template_path;
    String template_view = "remote";
    String remote_catalog_url;
    String pending_catalog_url;
    String active_template_id;
    String active_template_path;
    String active_template_display_name;
    String active_template_origin;
    Dictionary local_template_records;
    Dictionary pending_template;
    bool pending_template_activate = true;

    Error parse_templates_catalog(const String& json_content);
    TemplateVersion parse_version_entry(const String& godot_major, const String& version, const String& filename);
    String build_versions_url() const;
    String build_download_url(const String& filename) const;
    String get_download_cache_path(const String& filename) const;
    String get_local_versions_cache_path() const;

    void update_download_state(const String& filename, const String& state, float progress = 0.0f, const String& note = "");

    bool is_template_available_anywhere(const String& filename) const;
    Error save_versions_to_local_cache();
    Error http_get_sync_follow_redirects(const String& url, PackedByteArray& r_body, int& r_response_code, Dictionary* r_response_headers = nullptr, const std::function<void(int64_t downloaded, int64_t total)>& progress_callback = {}) const;
    Dictionary normalize_template_entry(const Variant& entry, const String& fallback_release_tag = "") const;
    String find_release_tag_for_filename(const String& filename) const;
    String find_sha256_for_filename(const String& filename) const;
    bool verify_template_file(const String& filename, const String& path) const;
    String get_latest_version_for_minor_line(const String& target_version, const String& major_version) const;
    int compare_version_numbers(const String& version1, const String& version2) const;
    Array parse_version_components(const String& version) const;
    String get_global_template_cache_root() const;
    Array build_available_versions_from_cache() const;
    String get_editor_version_line() const;
    String get_active_template_filename() const;
    String get_custom_template_cache_path(const String& url) const;
    void collect_local_template_files(const String& root, const String& fallback_origin, Array& choices, Dictionary& seen_paths) const;
    void migrate_legacy_template_cache();
    Dictionary find_template_choice(const String& template_id, bool include_local) const;
    Error activate_local_template(const Dictionary& choice, const String& path);
    void register_local_template(const Dictionary& choice, const String& path);
    void complete_pending_template(const String& path);
    Dictionary make_imported_template_choice(const String& source_path, const String& cached_path) const;
    bool validate_template_archive(const String& path) const;
    Error publish_download_atomically(const String& temporary_path, const String& output_path) const;
    void load_active_template_selection();
    void persist_active_template_selection() const;
    void ensure_default_active_template();

    // Signal handlers
    void _on_versions_download_completed(int p_result, int p_response_code, const PackedStringArray& p_headers, const PackedByteArray& p_body, const String& request_url);

    HTTPRequest* get_template_request() const;
    void release_template_request(bool cancel_request);
    void cancel_active_template_request();
    void _on_template_download_request_completed(int result, int response_code, const PackedStringArray& headers, const PackedByteArray& body);
};

} // namespace templates
} // namespace toolkit
