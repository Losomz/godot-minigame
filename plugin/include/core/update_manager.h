#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/core/object.hpp>

namespace toolkit {

class UpdateManager : public godot::Object {
	GDCLASS(UpdateManager, godot::Object);

public:
	enum UpdateState {
		STATE_IDLE,
		STATE_CHECKING,
		STATE_UPDATE_AVAILABLE,
		STATE_DOWNLOADING,
		STATE_DOWNLOADED,
		STATE_INSTALLING,
		STATE_UP_TO_DATE,
		STATE_ERROR,
	};

private:
	static UpdateManager *singleton;
	godot::HTTPRequest *version_checker;
	godot::HTTPRequest *downloader;

	// Track HTTP nodes for proper cleanup
	godot::Array active_http_nodes;
	godot::String download_file_path;
	godot::String expected_download_sha256;
	godot::String last_install_message;
	godot::String update_channel = "remote";
	godot::String last_local_package_path;
	bool plugin_state_loaded = false;

	godot::String local_version;
	godot::Dictionary remote_version_info;
	UpdateState current_state = STATE_IDLE;

	void _on_version_check_completed(int p_result, int p_response_code, const godot::PackedStringArray &p_headers, const godot::PackedByteArray &p_body);
	void _on_download_completed(int p_result, int p_response_code, const godot::PackedStringArray &p_headers, const godot::PackedByteArray &p_body);
	void set_state(UpdateState p_state);

	// HTTP nodes lifecycle management
	void track_http_node(godot::Node* node);
	void cleanup_http_nodes();
	godot::String resolve_update_manifest_url() const;
	godot::String resolve_update_asset_url(const godot::Dictionary &platform_data) const;
	godot::String resolve_platform_asset_name(const godot::Dictionary &platform_data) const;
	bool validate_update_package(const godot::String &p_path, godot::String &r_version, godot::String &r_error) const;

	godot::String get_update_cache_root() const;
	void load_plugin_state();
	void persist_plugin_state() const;
	void load_last_install_result();

protected:
	static void _bind_methods();

public:
	static UpdateManager *get_singleton();

	UpdateManager();
	~UpdateManager();

	void initialize();
	void check_for_updates(const godot::String &p_local_version);
	void download_update();
	void cancel_download();
	godot::Dictionary select_local_package(const godot::String &p_path, const godot::String &p_local_version);
	void set_update_channel(const godot::String &p_channel);
	godot::String get_update_channel() const;
	godot::String get_last_local_package_path() const;
	void clear_pending_update();
	bool prepare_update_and_restart(godot::String &r_error);
	void restart_editor_for_update();

	bool is_properly_configured() const;
	godot::String get_download_file_path() const;
	godot::String get_local_version() const;
	godot::Dictionary get_remote_version_info() const;
	godot::String get_last_install_message() const;

	UpdateState get_current_state() const;

	// Signals
	enum {
		SIGNAL_UPDATE_AVAILABLE = 0,
		SIGNAL_DOWNLOAD_FINISHED,
		SIGNAL_ERROR,
		SIGNAL_DOWNLOAD_PROGRESS_CHANGED,
		SIGNAL_UPDATE_STATE_CHANGED,
	};
};

} // namespace toolkit

VARIANT_ENUM_CAST(toolkit::UpdateManager::UpdateState);

#endif // UPDATE_MANAGER_H
