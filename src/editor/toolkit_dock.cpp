#include "editor/toolkit_dock.h"
#include "editor/settings_panel.h"
#include "templates/template_manager.h"
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/style_box_empty.hpp>
#include <godot_cpp/classes/engine.hpp>
#include "core/logging.h"

using namespace godot;

namespace toolkit
{
    namespace editor
    {

        GodotMinigameDock::GodotMinigameDock()
        {
            set_name("Minigame");
            // 设置边距，完全参考Godot调试面板的样式
            // 使用更合适的边距值，适合编辑器面板
            add_theme_constant_override("margin_left", -4);
            add_theme_constant_override("margin_right", -4);
            add_theme_constant_override("margin_top", -4);
        }

        GodotMinigameDock::~GodotMinigameDock()
        {
            // Cleanup handled by Godot
        }

        void GodotMinigameDock::_bind_methods()
        {
            ClassDB::bind_method(D_METHOD("create_panels"), &GodotMinigameDock::create_panels);
            ClassDB::bind_method(D_METHOD("update_panels"), &GodotMinigameDock::update_panels);
            ClassDB::bind_method(D_METHOD("refresh_all_panels"), &GodotMinigameDock::refresh_all_panels);
        }

        void GodotMinigameDock::_ready()
        {
            TOOLKIT_LOG("GodotMinigameDock: Ready");
            
            if (templates::TemplateManager::get_singleton()) {
                templates::TemplateManager::get_singleton()->initialize_template_system();
            }

            create_panels();
        }

        void GodotMinigameDock::_exit_tree()
        {
            TOOLKIT_LOG("GodotMinigameDock: Exit tree");
        }

        void GodotMinigameDock::create_panels()
        {
            // 直接创建tab container，MarginContainer作为根节点提供边距
            tab_container = memnew(TabContainer);
            tab_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            tab_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
            tab_container->set_custom_minimum_size(Vector2(300, 200)); // 确保有足够空间显示所有标签

            Ref<StyleBoxEmpty> empty;
            empty.instantiate();
            tab_container->add_theme_stylebox_override("panel", empty);

            // 设置标签栏样式，类似编辑器面板
            tab_container->set_tabs_rearrange_group(-1); // 禁用拖拽重排序
            add_child(tab_container);

            // Create settings panel (设置)
            settings_panel = memnew(SettingsPanel);
            settings_panel->set_name(String::utf8("设置"));
            tab_container->add_child(settings_panel);

            TOOLKIT_LOG("GodotMinigameDock: All panels created successfully");
        }

        void GodotMinigameDock::update_panels()
        {
            // Update all panels with current data
            if (settings_panel)
            {
                // Update settings panel if needed
            }
        }

        void GodotMinigameDock::refresh_all_panels()
        {
            update_panels();
            TOOLKIT_LOG("GodotMinigameDock: All panels refreshed");
        }

    } // namespace editor
} // namespace toolkit
