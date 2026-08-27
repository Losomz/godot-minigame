#include "editor/toolkit_dock.h"
#include "editor/settings_panel.h"
#include "templates/template_manager.h"
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
            settings_panel = memnew(SettingsPanel);
            settings_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
            settings_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
            add_child(settings_panel);

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
