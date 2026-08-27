#include "editor/wechat_export_platform.h"
#include "editor/editor_utils.h"
#include "templates/template_manager.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
#include "core/logging.h"

using namespace godot;

namespace toolkit {
namespace editor {

static constexpr const char *NATIVE_AUDIO_EXPORT_STATUS_FILE = ".godot-native-audio-export-status.json";

void WeChatExportPlatform::_bind_methods() {
}

static float _get_export_logo_editor_scale() {
    EditorInterface *editor = EditorInterface::get_singleton();
    if (editor) {
        return editor->get_editor_scale();
    }
    return 1.0f;
}

static Ref<Texture2D> _load_wechat_logo_svg(const String &path) {
    PackedByteArray svg_data = FileAccess::get_file_as_bytes(path);
    if (svg_data.is_empty()) {
        return Ref<Texture2D>();
    }

    Ref<Image> image = memnew(Image);
    if (image.is_null()) {
        return Ref<Texture2D>();
    }

    Error err = image->load_svg_from_buffer(svg_data, _get_export_logo_editor_scale());
    if (err != OK) {
        return Ref<Texture2D>();
    }

    return ImageTexture::create_from_image(image);
}

static String _describe_export_error(Error p_error) {
    switch (p_error) {
        case OK:
            return "ok";
        case ERR_BUSY:
            return "busy";
        case ERR_FILE_NOT_FOUND:
            return "file_not_found";
        case ERR_FILE_CANT_WRITE:
            return "file_cant_write";
        case ERR_CANT_CONNECT:
            return "network_connect_failed";
        case ERR_TIMEOUT:
            return "network_timeout";
        case ERR_UNCONFIGURED:
            return "unconfigured";
        case ERR_INVALID_PARAMETER:
            return "invalid_parameter";
        default:
            return "unknown_error";
    }
}

static void _product_log(const String &message) {
    UtilityFunctions::print("[GodotMinigame] ", message);
}

static Error _clear_native_audio_export_status(const String &p_path) {
    const String status_path = p_path.path_join(NATIVE_AUDIO_EXPORT_STATUS_FILE);
    const String suffixes[] = { "", ".tmp", ".bak" };
    for (const String &suffix : suffixes) {
        const String candidate = status_path + suffix;
        if (!FileAccess::file_exists(candidate)) {
            continue;
        }
        const Error remove_error = DirAccess::remove_absolute(candidate);
        if (remove_error != OK) {
            return remove_error;
        }
    }
    return OK;
}

static Error _publish_file_atomically(const String &p_temporary_path, const String &p_target_path) {
    if (!FileAccess::file_exists(p_temporary_path)) {
        return ERR_FILE_NOT_FOUND;
    }

    const String backup_path = p_target_path + String(".bak");
    if (FileAccess::file_exists(backup_path)) {
        if (!FileAccess::file_exists(p_target_path)) {
            const Error restore_error = DirAccess::rename_absolute(backup_path, p_target_path);
            if (restore_error != OK) {
                return restore_error;
            }
        } else {
            const Error remove_error = DirAccess::remove_absolute(backup_path);
            if (remove_error != OK) {
                return remove_error;
            }
        }
    }

    const bool had_previous_file = FileAccess::file_exists(p_target_path);
    if (had_previous_file) {
        const Error backup_error = DirAccess::rename_absolute(p_target_path, backup_path);
        if (backup_error != OK) {
            return backup_error;
        }
    }

    const Error publish_error = DirAccess::rename_absolute(p_temporary_path, p_target_path);
    if (publish_error != OK) {
        if (had_previous_file && !FileAccess::file_exists(p_target_path) && FileAccess::file_exists(backup_path)) {
            DirAccess::rename_absolute(backup_path, p_target_path);
        }
        return publish_error;
    }

    if (FileAccess::file_exists(backup_path)) {
        DirAccess::remove_absolute(backup_path);
    }
    return OK;
}

static Error _prepare_native_audio_export_status(const String &p_path) {
    const String status_path = p_path.path_join(NATIVE_AUDIO_EXPORT_STATUS_FILE);
    if (!FileAccess::file_exists(status_path)) {
        return _clear_native_audio_export_status(p_path);
    }

    Ref<FileAccess> status_file = FileAccess::open(status_path, FileAccess::READ);
    if (status_file.is_null()) {
        return FileAccess::get_open_error();
    }
    const Variant parsed = JSON::parse_string(status_file->get_as_text());
    status_file->close();
    if (parsed.get_type() != Variant::DICTIONARY) {
        return ERR_PARSE_ERROR;
    }

    const Dictionary status = parsed;
    const String state = String(status.get("state", ""));
    const int64_t process_id = int64_t(status.get("process_id", int64_t(0)));
    const int64_t current_process_id = OS::get_singleton() == nullptr ? 0 : int64_t(OS::get_singleton()->get_process_id());
    if (state == "pending" && process_id != 0 && process_id == current_process_id) {
        return OK;
    }
    return _clear_native_audio_export_status(p_path);
}

static Error _validate_native_audio_export_status(const String &p_path, bool &r_plugin_exported) {
    r_plugin_exported = false;
    const String status_path = p_path.path_join(NATIVE_AUDIO_EXPORT_STATUS_FILE);
    if (!FileAccess::file_exists(status_path)) {
        return OK;
    }

    Ref<FileAccess> status_file = FileAccess::open(status_path, FileAccess::READ);
    if (status_file.is_null()) {
        return FileAccess::get_open_error();
    }
    const Variant parsed = JSON::parse_string(status_file->get_as_text());
    status_file->close();
    if (parsed.get_type() != Variant::DICTIONARY) {
        UtilityFunctions::push_error("WeChat native audio export status is invalid.");
        return ERR_PARSE_ERROR;
    }

    const Dictionary status = parsed;
    const String state = String(status.get("state", ""));
    const int64_t process_id = int64_t(status.get("process_id", int64_t(0)));
    const int64_t current_process_id = OS::get_singleton() == nullptr ? 0 : int64_t(OS::get_singleton()->get_process_id());
    if (state != "succeeded" || process_id == 0 || process_id != current_process_id) {
        const String message = String(status.get("message", "")).strip_edges();
        UtilityFunctions::push_error(message.is_empty()
                ? String("WeChat native audio export did not complete.")
                : message);
        return ERR_CANT_CREATE;
    }
    r_plugin_exported = true;
    return _clear_native_audio_export_status(p_path);
}

static bool _is_valid_native_audio_subpackage(const Dictionary &p_package) {
    const String name = String(p_package.get("name", "")).strip_edges();
    const String root = String(p_package.get("root", "")).replace("\\", "/").strip_edges();
    return !name.is_empty() && root.begins_with("subpackages/") && !root.contains("..");
}

static bool _is_managed_native_audio_asset_path(const String &p_path) {
    const String raw_path = p_path.replace("\\", "/").strip_edges();
    if (raw_path.is_empty() || raw_path.contains("..") || raw_path.is_absolute_path()) {
        return false;
    }
    const PackedStringArray parts = raw_path.simplify_path().split("/", false);
    const bool main_package_path = parts.size() == 2 && parts[0] == "native_audio";
    const bool subpackage_path = parts.size() == 4 && parts[0] == "subpackages" && !parts[1].is_empty() && parts[2] == "native_audio";
    if (!main_package_path && !subpackage_path) {
        return false;
    }

    const String file_name = parts[parts.size() - 1];
    const String hash = file_name.get_basename();
    const String codec = file_name.get_extension();
    if (hash.length() != 64 || codec.is_empty()) {
        return false;
    }
    for (int64_t i = 0; i < hash.length(); i++) {
        const char32_t character = hash[i];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    for (int64_t i = 0; i < codec.length(); i++) {
        const char32_t character = codec[i];
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'z'))) {
            return false;
        }
    }
    return true;
}

static bool _subpackage_matches(const Dictionary &p_package, const Array &p_candidates) {
    const String name = String(p_package.get("name", "")).strip_edges();
    const String root = String(p_package.get("root", "")).replace("\\", "/").strip_edges();
    for (int32_t i = 0; i < p_candidates.size(); i++) {
        if (p_candidates[i].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary candidate = p_candidates[i];
        if (!_is_valid_native_audio_subpackage(candidate)) {
            continue;
        }
        const String candidate_name = String(candidate.get("name", "")).strip_edges();
        const String candidate_root = String(candidate.get("root", "")).replace("\\", "/").strip_edges();
        if ((!name.is_empty() && name == candidate_name) || (!root.is_empty() && root == candidate_root)) {
            return true;
        }
    }
    return false;
}

static Error _update_game_subpackages(const String &p_game_json_path, const Array &p_remove_packages, const Array &p_add_packages) {
    Ref<FileAccess> game_file = FileAccess::open(p_game_json_path, FileAccess::READ);
    if (game_file.is_null()) {
        return FileAccess::get_open_error();
    }
    const Variant parsed_game = JSON::parse_string(game_file->get_as_text());
    game_file->close();
    if (parsed_game.get_type() != Variant::DICTIONARY) {
        return ERR_PARSE_ERROR;
    }

    Dictionary game = parsed_game;
    Array merged_subpackages;
    const Variant existing_value = game.get("subpackages", Array());
    if (existing_value.get_type() == Variant::ARRAY) {
        const Array existing = existing_value;
        for (int32_t i = 0; i < existing.size(); i++) {
            if (existing[i].get_type() == Variant::DICTIONARY && _subpackage_matches(existing[i], p_remove_packages)) {
                continue;
            }
            merged_subpackages.push_back(existing[i]);
        }
    }

    for (int32_t i = 0; i < p_add_packages.size(); i++) {
        if (p_add_packages[i].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary package = p_add_packages[i];
        if (!_is_valid_native_audio_subpackage(package) || _subpackage_matches(package, merged_subpackages)) {
            continue;
        }
        merged_subpackages.push_back(package);
    }

    game["subpackages"] = merged_subpackages;
    Ref<FileAccess> output = FileAccess::open(p_game_json_path, FileAccess::WRITE);
    if (output.is_null()) {
        return FileAccess::get_open_error();
    }
    output->store_string(JSON::stringify(game, "    "));
    output->close();
    return OK;
}

static Ref<Texture2D> _load_wechat_logo_fallback() {
    const char *logo_paths[] = {
        "res://resources/assets/icon.svg",
        "res://addons/godot-minigame/resources/assets/icon.svg",
        nullptr
    };

    for (int i = 0; logo_paths[i] != nullptr; i++) {
        Ref<Texture2D> tex = _load_wechat_logo_svg(String::utf8(logo_paths[i]));
        if (tex.is_valid()) {
            return tex;
        }
    }

    // Final fallback: create a placeholder so export platform entry is never icon-less.
    int logo_size = int(_get_export_logo_editor_scale() * 32.0f + 0.5f);
    if (logo_size < 1) {
        logo_size = 1;
    }
    Ref<Image> image = Image::create_empty(logo_size, logo_size, false, Image::FORMAT_RGBA8);
    if (image.is_valid()) {
        image->fill(Color(0.16, 0.67, 0.35, 1.0));
        return ImageTexture::create_from_image(image);
    }

    return Ref<Texture2D>();
}

static Node *_resolve_export_overlay_parent(EditorInterface *editor) {
    if (!editor) {
        return nullptr;
    }

    Control *base_control = editor->get_base_control();
    if (!base_control) {
        return nullptr;
    }

    SceneTree *tree = base_control->get_tree();
    if (!tree) {
        return base_control;
    }

    Node *root = tree->get_root();
    if (!root) {
        return base_control;
    }

    // Prefer ProjectExportDialog so overlay is visible above the export UI.
    Node *export_dialog = root->find_child("*ProjectExportDialog*", true, false);
    if (export_dialog) {
        return export_dialog;
    }

    return base_control;
}

void WeChatExportPlatform::_show_export_progress_dialog() {
    _hide_download_progress_dialog();

    if (!Engine::get_singleton()->is_editor_hint()) {
        return;
    }

    EditorInterface *editor = EditorInterface::get_singleton();
    if (!editor) {
        return;
    }
    Node *parent = _resolve_export_overlay_parent(editor);
    if (!parent) {
        return;
    }

    export_progress_value = 0.0;

    download_overlay = memnew(Control);
    download_overlay->set_name("GodotMinigameDownloadOverlay");
    download_overlay->set_anchors_preset(Control::PRESET_FULL_RECT);
    download_overlay->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
    parent->add_child(download_overlay);
    parent->move_child(download_overlay, parent->get_child_count() - 1);

    download_panel = memnew(PanelContainer);
    download_panel->set_name("GodotMinigameDownloadPanel");
    download_panel->set_anchors_preset(Control::PRESET_CENTER);
    download_panel->set_offset(SIDE_LEFT, -230.0);
    download_panel->set_offset(SIDE_TOP, -70.0);
    download_panel->set_offset(SIDE_RIGHT, 230.0);
    download_panel->set_offset(SIDE_BOTTOM, 70.0);
    download_panel->set_mouse_filter(Control::MOUSE_FILTER_STOP);
    download_overlay->add_child(download_panel);

    VBoxContainer *layout = memnew(VBoxContainer);
    layout->set_anchors_preset(Control::PRESET_FULL_RECT);
    layout->set_offset(SIDE_LEFT, 16.0);
    layout->set_offset(SIDE_TOP, 14.0);
    layout->set_offset(SIDE_RIGHT, -16.0);
    layout->set_offset(SIDE_BOTTOM, -14.0);
    layout->add_theme_constant_override("separation", 10);

    download_status_label = memnew(Label);
    download_status_label->set_text(String::utf8("准备导出..."));
    layout->add_child(download_status_label);

    download_progress_bar = memnew(ProgressBar);
    download_progress_bar->set_min(0.0);
    download_progress_bar->set_max(100.0);
    download_progress_bar->set_value(0.0);
    download_progress_bar->set_show_percentage(true);
    download_progress_bar->set_custom_minimum_size(Vector2(420, 24));
    layout->add_child(download_progress_bar);

    download_panel->add_child(layout);

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->force_draw(false, 0.0);
    }
}

void WeChatExportPlatform::_hide_download_progress_dialog() {
    if (download_overlay) {
        download_overlay->queue_free();
        download_overlay = nullptr;
    }
    download_panel = nullptr;
    download_status_label = nullptr;
    download_progress_bar = nullptr;
    export_progress_value = 0.0;

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->force_draw(false, 0.0);
    }
}

void WeChatExportPlatform::_set_export_progress(double p_progress, const String &p_text) {
    double clamped = p_progress;
    if (clamped < 0.0) {
        clamped = 0.0;
    }
    if (clamped > 100.0) {
        clamped = 100.0;
    }

    export_progress_value = clamped;

    if (download_progress_bar) {
        download_progress_bar->set_value(clamped);
    }
    if (download_status_label && !p_text.is_empty()) {
        download_status_label->set_text(p_text);
    }

    RenderingServer *rs = RenderingServer::get_singleton();
    if (rs) {
        rs->force_draw(false, 0.0);
    }
}

void WeChatExportPlatform::_simulate_export_progress(double p_from, double p_to, int p_duration_ms, const String &p_text) {
    if (!download_overlay) {
        return;
    }

    int steps = p_duration_ms / 40;
    if (steps < 1) {
        steps = 1;
    }

    for (int i = 1; i <= steps; i++) {
        double t = double(i) / double(steps);
        double value = p_from + (p_to - p_from) * t;
        _set_export_progress(value, p_text);
        OS::get_singleton()->delay_msec(40);
    }
}

PackedStringArray WeChatExportPlatform::_get_preset_features(const Ref<EditorExportPreset> &p_preset) const {
    PackedStringArray features;
    features.append("web");
    features.append("wechat");
    features.append("wasm");
    return features;
}

TypedArray<Dictionary> WeChatExportPlatform::_get_export_options() const {
    TypedArray<Dictionary> options;

    // 微信小游戏基本信息
    Dictionary appid;
    appid["name"] = String::utf8("微信小游戏/游戏_AppID");
    appid["type"] = Variant::STRING;
    appid["default_value"] = "wxf40904ea6120ad08";
    options.append(appid);

    Dictionary project_name;
    project_name["name"] = String::utf8("微信小游戏/小游戏项目名");
    project_name["type"] = Variant::STRING;
    project_name["default_value"] = "GodotMiniGame";
    options.append(project_name);

    Dictionary orientation;
    orientation["name"] = String::utf8("微信小游戏/游戏方向");
    orientation["type"] = Variant::STRING;
    orientation["hint"] = PROPERTY_HINT_ENUM;
    orientation["hint_string"] = "portrait,landscape";
    orientation["default_value"] = "portrait";
    options.append(orientation);

    // 资源信息
    Dictionary cover_image;
    cover_image["name"] = String::utf8("资源信息/启动封面背景图");
    cover_image["type"] = Variant::STRING;
    cover_image["hint"] = PROPERTY_HINT_FILE;
    cover_image["hint_string"] = "*.png,*.jpg,*.jpeg";
    cover_image["default_value"] = "";
    options.append(cover_image);

    Dictionary logo_image;
    logo_image["name"] = String::utf8("资源信息/启动封面logo");
    logo_image["type"] = Variant::STRING;
    logo_image["hint"] = PROPERTY_HINT_FILE;
    logo_image["hint_string"] = "*.png,*.jpg,*.jpeg";
    logo_image["default_value"] = "";
    options.append(logo_image);

    Dictionary native_audio_duration;
    native_audio_duration["name"] = String::utf8("音频设置/长音频原生播放阈值（秒）");
    native_audio_duration["type"] = Variant::FLOAT;
    native_audio_duration["hint"] = PROPERTY_HINT_RANGE;
    native_audio_duration["hint_string"] = "0,60,0.1,or_greater,suffix:s";
    native_audio_duration["default_value"] = 3.0;
    options.append(native_audio_duration);

    Dictionary native_audio_cache_limit;
    native_audio_cache_limit["name"] = String::utf8("音频设置/原生音频缓存上限（MiB）");
    native_audio_cache_limit["type"] = Variant::INT;
    native_audio_cache_limit["hint"] = PROPERTY_HINT_RANGE;
    native_audio_cache_limit["hint_string"] = "1,64,1,or_greater,suffix: MiB";
    native_audio_cache_limit["default_value"] = 8;
    options.append(native_audio_cache_limit);

    return options;
}

String WeChatExportPlatform::_get_name() const {
    return String::utf8("小游戏");
}

String WeChatExportPlatform::_get_os_name() const {
    return "WeChatMiniGame";
}

Ref<Texture2D> WeChatExportPlatform::_get_logo() const {
    return logo;
}

Error WeChatExportPlatform::_export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags) {
    TOOLKIT_LOG("WeChatExportPlatform: Starting project export to ", p_path);

    String export_dir = p_path;
    if (!p_path.get_extension().is_empty()) {
        export_dir = p_path.get_base_dir();
    }

    Ref<DirAccess> da = DirAccess::open("res://");
    if (da.is_valid()) {
        da->make_dir_recursive(export_dir);
    }

    _show_export_progress_dialog();
    _set_export_progress(2.0, String::utf8("准备导出..."));
    _simulate_export_progress(2.0, 12.0, 220, String::utf8("正在检查导出环境..."));

    // 1. 设置模板
    Error err = _setup_wechat_template(p_preset, export_dir);
    if (err != OK) {
        _set_export_progress(100.0, String::utf8("导出失败"));
        OS::get_singleton()->delay_msec(180);
        _hide_download_progress_dialog();
        return err;
    }

    if (export_progress_value < 70.0) {
        _simulate_export_progress(export_progress_value, 70.0, 260, String::utf8("模板就绪，准备打包..."));
    }

    err = _prepare_native_audio_runtime(p_preset, export_dir);
    if (err != OK) {
        _set_export_progress(100.0, String::utf8("导出失败"));
        _hide_download_progress_dialog();
        return err;
    }

    // 2. 导出资源包到 engine/demo-pck.bin
    String engine_dir = export_dir.path_join("engine");
    if (da.is_valid() && !da->dir_exists(engine_dir)) {
        da->make_dir_recursive(engine_dir);
    }

    String res_path = engine_dir.path_join("demo-pck.bin");
    String temporary_pack_path = export_dir.path_join(".demo-pck.bin.tmp");
    if (FileAccess::file_exists(temporary_pack_path)) {
        DirAccess::remove_absolute(temporary_pack_path);
    }
    TOOLKIT_LOG("WeChatExportPlatform: Exporting main pack via ", temporary_pack_path);
    _simulate_export_progress(export_progress_value, 92.0, 280, String::utf8("正在打包资源..."));
    err = export_pack(p_preset, p_debug, temporary_pack_path, p_flags);
    if (err != OK) {
        _set_export_progress(100.0, String::utf8("资源打包失败"));
        _hide_download_progress_dialog();
        return err;
    }
    bool native_audio_plugin_exported = false;
    err = _validate_native_audio_export_status(export_dir, native_audio_plugin_exported);
    if (err != OK) {
        DirAccess::remove_absolute(temporary_pack_path);
        _set_export_progress(100.0, String::utf8("音频资源发布失败"));
        _hide_download_progress_dialog();
        return err;
    }
    err = _finalize_native_audio_runtime(export_dir, native_audio_plugin_exported);
    if (err != OK) {
        DirAccess::remove_absolute(temporary_pack_path);
        _set_export_progress(100.0, String::utf8("音频资源发布失败"));
        _hide_download_progress_dialog();
        return err;
    }
    err = _publish_file_atomically(temporary_pack_path, res_path);
    if (err != OK) {
        _set_export_progress(100.0, String::utf8("资源包发布失败"));
        _hide_download_progress_dialog();
        return err;
    }
    _set_export_progress(100.0, String::utf8("导出完成"));
    OS::get_singleton()->delay_msec(200);
    _hide_download_progress_dialog();

    TOOLKIT_LOG("WeChatExportPlatform: Project export completed.");
    return OK;
}

Error WeChatExportPlatform::_export_pack(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags) {
    // 拦截“导出 PCK/ZIP”按钮
    String target_path = p_path;
    if (p_path.get_extension() == "pck") {
        target_path = p_path.get_basename() + ".bin";
    }

    TOOLKIT_LOG("WeChatExportPlatform: Intercepted resource export to: ", target_path);

    String project_dir = target_path.get_base_dir();
    if (FileAccess::file_exists(project_dir.path_join("game.json"))) {
        _modify_json_configs(p_preset, project_dir);
        _copy_export_images(p_preset, project_dir);
    }

    const Dictionary pack_result = save_pack(p_preset, p_debug, target_path);
    return Error(int64_t(pack_result.get("result", FAILED)));
}

Error WeChatExportPlatform::_export_zip(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags) {
    TOOLKIT_LOG("WeChatExportPlatform: Exporting as ZIP to: ", p_path);
    save_zip(p_preset, p_debug, p_path);
    return OK;
}

Error WeChatExportPlatform::_setup_wechat_template(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
    String game_json_path = p_path.path_join("game.json");

    if (!FileAccess::file_exists(game_json_path)) {
        TOOLKIT_LOG("WeChatExportPlatform: Setting up template at ", p_path);

        if (!Engine::get_singleton()->has_singleton("TemplateManager")) {
            UtilityFunctions::push_warning("TemplateManager missing.");
            return ERR_UNCONFIGURED;
        }

        templates::TemplateManager* tm = templates::TemplateManager::get_singleton();
        Error init_err = tm->initialize_template_system();
        if (init_err != OK) {
            String msg = "Template system init failed: " + _describe_export_error(init_err) + " (" + String::num_int64(init_err) + ")";
            UtilityFunctions::push_warning(msg);
            TOOLKIT_LOG("WeChatExportPlatform: ", msg);
            return init_err;
        }
        const Dictionary active_template = tm->get_active_template_info();
        const String best_template_ref = tm->resolve_active_template_path();
        _product_log("Active template: " + String(active_template.get("display_name", "unknown")) +
                ", editor " + tm->get_current_godot_version());

        if (best_template_ref.is_empty()) {
            UtilityFunctions::push_warning(String::utf8("当前模板尚未缓存或与此 Godot 版本不兼容。请在 Minigame 插件设置中选择并下载模板。"));
            return ERR_FILE_NOT_FOUND;
        }
        _product_log("Resolved template: " + best_template_ref.get_file());

        TOOLKIT_LOG("WeChatExportPlatform: extracting template from ", best_template_ref);
        _simulate_export_progress(export_progress_value, 88.0, 220, String::utf8("正在解压模板..."));
        Error extract_err = tm->extract_template(best_template_ref, p_path);
        if (extract_err != OK) {
            String msg = "Template extraction failed: " + _describe_export_error(extract_err) + " (" + String::num_int64(extract_err) + ")";
            UtilityFunctions::push_warning(msg);
            TOOLKIT_LOG("WeChatExportPlatform: ", msg);
            return extract_err;
        }

        _modify_json_configs(p_preset, p_path);
        _copy_export_images(p_preset, p_path);
    }
    return OK;
}

void WeChatExportPlatform::_modify_json_configs(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
    String project_config_path = p_path.path_join("project.config.json");
    if (FileAccess::file_exists(project_config_path)) {
        Ref<FileAccess> f = FileAccess::open(project_config_path, FileAccess::READ);
        if (f.is_valid()) {
            String content = f->get_as_text();
            f->close();

            Variant json_var = JSON::parse_string(content);
            if (json_var.get_type() == Variant::DICTIONARY) {
                Dictionary dict = json_var;
                dict["appid"] = p_preset->get(String::utf8("微信小游戏/游戏_AppID"));
                dict["projectname"] = p_preset->get(String::utf8("微信小游戏/小游戏项目名"));

                String new_content = JSON::stringify(dict, "    ");
                f = FileAccess::open(project_config_path, FileAccess::WRITE);
                if (f.is_valid()) {
                    f->store_string(new_content);
                    f->close();
                }
            }
        }
    }

    String game_json_path = p_path.path_join("game.json");
    if (FileAccess::file_exists(game_json_path)) {
        Ref<FileAccess> f = FileAccess::open(game_json_path, FileAccess::READ);
        if (f.is_valid()) {
            String content = f->get_as_text();
            f->close();

            Variant json_var = JSON::parse_string(content);
            if (json_var.get_type() == Variant::DICTIONARY) {
                Dictionary dict = json_var;
                dict["deviceOrientation"] = p_preset->get(String::utf8("微信小游戏/游戏方向"));

                String new_content = JSON::stringify(dict, "    ");
                f = FileAccess::open(game_json_path, FileAccess::WRITE);
                if (f.is_valid()) {
                    f->store_string(new_content);
                    f->close();
                }
            }
        }
    }
}

void WeChatExportPlatform::_copy_export_images(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
    String images_dir = p_path.path_join("images");
    Ref<DirAccess> da = DirAccess::open("res://");
    if (da.is_valid() && !da->dir_exists(images_dir)) {
        da->make_dir_recursive(images_dir);
    }

    String cover_src = p_preset->get(String::utf8("资源信息/启动封面背景图"));
    if (!cover_src.is_empty() && da.is_valid()) {
        String cover_dst = images_dir.path_join("background.jpg");
        da->copy(cover_src, cover_dst);
    }

    String logo_src = p_preset->get(String::utf8("资源信息/启动封面logo"));
    if (!logo_src.is_empty() && da.is_valid()) {
        String logo_dst = images_dir.path_join("logo.png");
        da->copy(logo_src, logo_dst);
    }
}

PackedStringArray WeChatExportPlatform::_get_platform_features() const {
    PackedStringArray features;
    features.append("wechat");
    return features;
}

PackedStringArray WeChatExportPlatform::_get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
    PackedStringArray extensions;
    extensions.append("bin");
    return extensions;
}

bool WeChatExportPlatform::_has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, bool p_debug) const {
    return true;
}

bool WeChatExportPlatform::_has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset) const {
    return true;
}

String WeChatExportPlatform::_get_export_option_warning(const Ref<EditorExportPreset> &p_preset, const StringName &p_name) const {
    return "";
}

bool WeChatExportPlatform::_get_export_option_visibility(const Ref<EditorExportPreset> &p_preset, const String &p_option) const {
    return true;
}

WeChatExportPlatform::WeChatExportPlatform() {
    logo = load_embedded_icon("resources/assets/icon.svg");
    if (!logo.is_valid()) {
        logo = _load_wechat_logo_fallback();
    }
    if (!logo.is_valid()) {
        UtilityFunctions::printerr("[GodotMinigame][WeChatExportPlatform] logo is still null after all fallbacks");
    }
}

Error WeChatExportPlatform::_prepare_native_audio_runtime(const Ref<EditorExportPreset> &p_preset, const String &p_path) {
    previous_native_audio_subpackages.clear();
    previous_native_audio_output_paths.clear();
    const Error status_error = _prepare_native_audio_export_status(p_path);
    if (status_error != OK) {
        return status_error;
    }

    const String game_script_path = p_path.path_join("game.js");
    Ref<FileAccess> game_script_file = FileAccess::open(game_script_path, FileAccess::READ);
    if (game_script_file.is_null()) {
        return FileAccess::get_open_error();
    }
    String game_script = game_script_file->get_as_text();
    game_script_file->close();
    String import_prefix;
    if (!game_script.contains("native-audio-config")) {
        import_prefix += "import './native-audio-config'\n";
    }
    if (!game_script.contains("native-audio-manifest")) {
        import_prefix += "import './native-audio-manifest'\n";
    }
    if (!import_prefix.is_empty()) {
        game_script_file = FileAccess::open(game_script_path, FileAccess::WRITE);
        if (game_script_file.is_null()) {
            return FileAccess::get_open_error();
        }
        game_script_file->store_string(import_prefix + game_script);
        game_script_file->close();
    }

    const String manifest_json_path = p_path.path_join("native-audio-manifest.json");
    String previous_manifest_json;
    if (FileAccess::file_exists(manifest_json_path)) {
        Ref<FileAccess> manifest_file = FileAccess::open(manifest_json_path, FileAccess::READ);
        if (manifest_file.is_null()) {
            return FileAccess::get_open_error();
        }
        previous_manifest_json = manifest_file->get_as_text();
        manifest_file->close();
        const Variant parsed = JSON::parse_string(previous_manifest_json);
        if (parsed.get_type() != Variant::DICTIONARY) {
            return ERR_PARSE_ERROR;
        }

        const Dictionary root = parsed;
        const Variant subpackages_value = root.get("subpackages", Array());
        if (subpackages_value.get_type() == Variant::ARRAY) {
            previous_native_audio_subpackages = subpackages_value;
        }
        const Variant assets_value = root.get("assets", Dictionary());
        if (assets_value.get_type() == Variant::DICTIONARY) {
            const Dictionary assets = assets_value;
            const Array hashes = assets.keys();
            for (int32_t i = 0; i < hashes.size(); i++) {
                const Variant asset_value = assets[hashes[i]];
                if (asset_value.get_type() != Variant::DICTIONARY) {
                    continue;
                }
                const Dictionary asset = asset_value;
                const String manifest_path = String(asset.get("src", ""));
                if (_is_managed_native_audio_asset_path(manifest_path)) {
                    const String relative_path = manifest_path.replace("\\", "/").simplify_path();
                    if (previous_native_audio_output_paths.find(relative_path) < 0) {
                        previous_native_audio_output_paths.push_back(relative_path);
                    }
                }
            }
        }
    }

    const double duration_seconds = MAX(0.0, double(p_preset->get(String::utf8("音频设置/长音频原生播放阈值（秒）"))));
    const int64_t cache_limit_mib = MAX(int64_t(1), int64_t(p_preset->get(String::utf8("音频设置/原生音频缓存上限（MiB）"))));
    const int64_t cache_limit_bytes = cache_limit_mib * 1024 * 1024;
    const String config_script =
            "GameGlobal.__godotMinigameNativeAudioMinDurationSeconds = " + String::num(duration_seconds, 3) + ";\n" +
            "GameGlobal.__godotMinigameNativeAudioCacheLimitBytes = " + String::num_int64(cache_limit_bytes) + ";\n";
    Ref<FileAccess> config_file = FileAccess::open(p_path.path_join("native-audio-config.js"), FileAccess::WRITE);
    if (config_file.is_null()) {
        return FileAccess::get_open_error();
    }
    config_file->store_string(config_script);
    config_file->close();

    const String manifest_script_path = p_path.path_join("native-audio-manifest.js");
    if (!FileAccess::file_exists(manifest_script_path)) {
        const String manifest_script = previous_manifest_json.is_empty()
                ? String("GameGlobal.__godotMinigameNativeAudioManifest = {\"version\":1,\"aliases\":{},\"assets\":{},\"subpackages\":[]};\n")
                : String("GameGlobal.__godotMinigameNativeAudioManifest = ") + previous_manifest_json + ";\n";
        Ref<FileAccess> manifest_script_file = FileAccess::open(manifest_script_path, FileAccess::WRITE);
        if (manifest_script_file.is_null()) {
            return FileAccess::get_open_error();
        }
        manifest_script_file->store_string(manifest_script);
        manifest_script_file->close();
    }
    return OK;
}

Error WeChatExportPlatform::_finalize_native_audio_runtime(const String &p_path, bool p_plugin_exported) {
    if (p_plugin_exported) {
        return _merge_native_audio_subpackages(p_path);
    }

    const Error update_error = _update_game_subpackages(
            p_path.path_join("game.json"), previous_native_audio_subpackages, Array());
    if (update_error != OK) {
        return update_error;
    }

    const String empty_manifest_script =
            "GameGlobal.__godotMinigameNativeAudioManifest = {\"version\":1,\"aliases\":{},\"assets\":{},\"subpackages\":[]};\n";
    Ref<FileAccess> manifest_script_file = FileAccess::open(p_path.path_join("native-audio-manifest.js"), FileAccess::WRITE);
    if (manifest_script_file.is_null()) {
        return FileAccess::get_open_error();
    }
    manifest_script_file->store_string(empty_manifest_script);
    manifest_script_file->close();

    const String manifest_json_path = p_path.path_join("native-audio-manifest.json");
    if (FileAccess::file_exists(manifest_json_path)) {
        const Error remove_error = DirAccess::remove_absolute(manifest_json_path);
        if (remove_error != OK) {
            return remove_error;
        }
    }
    for (int32_t i = 0; i < previous_native_audio_output_paths.size(); i++) {
        const String absolute_path = p_path.path_join(previous_native_audio_output_paths[i]);
        if (!FileAccess::file_exists(absolute_path)) {
            continue;
        }
        const Error remove_error = DirAccess::remove_absolute(absolute_path);
        if (remove_error != OK) {
            return remove_error;
        }
    }
    return OK;
}

Error WeChatExportPlatform::_merge_native_audio_subpackages(const String &p_path) {
    Array native_audio_subpackages;
    const String manifest_path = p_path.path_join("native-audio-manifest.json");
    if (FileAccess::file_exists(manifest_path)) {
        Ref<FileAccess> manifest_file = FileAccess::open(manifest_path, FileAccess::READ);
        if (manifest_file.is_null()) {
            return FileAccess::get_open_error();
        }
        const Variant parsed = JSON::parse_string(manifest_file->get_as_text());
        manifest_file->close();
        if (parsed.get_type() != Variant::DICTIONARY) {
            return ERR_PARSE_ERROR;
        }
        const Dictionary manifest = parsed;
        const Variant subpackages_value = manifest.get("subpackages", Array());
        if (subpackages_value.get_type() == Variant::ARRAY) {
            native_audio_subpackages = subpackages_value;
        }
    }

    return _update_game_subpackages(
            p_path.path_join("game.json"), previous_native_audio_subpackages, native_audio_subpackages);
}

WeChatExportPlatform::~WeChatExportPlatform() {
    _hide_download_progress_dialog();
}

} // namespace editor
} // namespace toolkit
