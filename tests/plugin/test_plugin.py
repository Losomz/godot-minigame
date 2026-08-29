from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/product"))

import product  # noqa: E402


class ProductToolTests(unittest.TestCase):
    def test_repository_contracts_are_valid(self):
        product.validate_repository(ROOT)

    def test_versions_projection_matches_embedded_fallback(self):
        catalog = product.load_json(ROOT / "plugin/catalog/templates.json")
        rendered = product.render_versions(catalog)
        current = (ROOT / "plugin/resources/versions.yaml").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(rendered, current)

    def test_invalid_semver_is_rejected(self):
        with self.assertRaises(product.ProductError):
            product.parse_semver("1.2", "test version")

    def test_product_has_two_release_catalogs(self):
        plugin = product.load_json(ROOT / "plugin/plugin.json")
        adapters = product.load_json(ROOT / "adapter/adapters.json")
        self.assertEqual(plugin["update_catalog"], "plugin/catalog/plugin-stable.json")
        self.assertTrue(adapters["adapters"])
        for adapter in adapters["adapters"]:
            self.assertNotIn(adapter["branch"], {"main", "develop", "upstream-sync"})
            build = adapter.get("build")
            if build:
                self.assertTrue((ROOT / ".github/workflows" / build["workflow"]).is_file())

        experimental = next(item for item in adapters["adapters"] if item["branch"] == "4.6")
        self.assertEqual(experimental["status"], "experimental")
        self.assertNotIn("build", experimental)

    def test_staged_plugin_version_may_lead_published_stable_catalog(self):
        plugin = product.load_json(ROOT / "plugin/plugin.json")
        stable = product.load_json(ROOT / "plugin/catalog/plugin-stable.json")
        self.assertGreaterEqual(
            product.parse_semver(plugin["version"], "plugin"),
            product.parse_semver(stable["version"], "stable"),
        )
        self.assertEqual(stable["tag"], f"plugin-v{stable['version']}")

    def test_runtime_catalog_and_global_template_selection_contracts_are_present(self):
        source = (ROOT / "plugin/src/templates/template_manager.cpp").read_text(encoding="utf-8")
        header = (ROOT / "plugin/include/templates/template_manager.h").read_text(encoding="utf-8")
        export_source = (ROOT / "plugin/src/editor/wechat_export_platform.cpp").read_text(encoding="utf-8")
        settings_source = (ROOT / "plugin/src/editor/settings_panel.cpp").read_text(encoding="utf-8")
        self.assertIn('status != "stable"', source)
        self.assertIn('minimum_plugin', source)
        self.assertIn('FileAccess::get_sha256', source)
        self.assertIn('catalog/templates.json', source)
        self.assertNotIn('parse_versions_yaml', source)
        self.assertIn('get_active_template_info()', export_source)
        self.assertIn('resolve_active_template_path()', export_source)
        self.assertNotIn('模板/模板版本', export_source)
        self.assertNotIn('模板/模板来源', export_source)
        self.assertIn('set_active_catalog_template', settings_source)
        self.assertIn('remove_active_template_cache', settings_source)
        self.assertIn('clear_all_template_cache', settings_source)
        self.assertIn('set_download_file(temporary_path)', source)
        self.assertIn('TemplateManager_TemplateRequest', source)
        self.assertIn('HTTPRequest *request = memnew(HTTPRequest)', source)
        self.assertIn('set_download_file(temporary_path)', source)
        self.assertIn('publish_download_atomically(temporary_path, output_path)', source)
        self.assertNotIn('prefetch_thread', header)
        self.assertNotIn('_prefetch_worker', source)
        self.assertNotIn('classes/thread.hpp', header)
        self.assertNotIn('Timer', header)
        self.assertIn('class TemplateManager : public Object', header)
        self.assertNotIn('class TemplateManager : public RefCounted', header)
        self.assertIn('EditorFileDialog::FILE_MODE_OPEN_FILE', settings_source)
        self.assertIn('add_filter("*.tpz"', settings_source)
        self.assertIn('set_indeterminate(true)', settings_source)

    def test_plugin_update_requires_confirmation_and_relaunches_project(self):
        update_source = (ROOT / "plugin/src/core/update_manager.cpp").read_text(encoding="utf-8")
        update_header = (ROOT / "plugin/include/core/update_manager.h").read_text(encoding="utf-8")
        settings_source = (ROOT / "plugin/src/editor/settings_panel.cpp").read_text(encoding="utf-8")
        helper_source = (ROOT / "plugin/addon/update_helper.gd").read_text(encoding="utf-8")
        self.assertIn("STATE_INSTALLING", update_header)
        self.assertIn("set_state(STATE_DOWNLOADED)", update_source)
        self.assertNotIn('call_deferred("install_downloaded_update")', update_source)
        self.assertIn("restart_editor_for_update()", settings_source)
        self.assertIn("安装并重启", settings_source)
        self.assertIn("OS.is_process_running(parent_pid)", helper_source)
        self.assertIn("DirAccess.rename_absolute(addon_path, backup_path)", helper_source)
        self.assertIn("DirAccess.rename_absolute(backup_path, addon_path)", helper_source)
        self.assertIn('PackedStringArray(["--editor", "--path", project_path])', helper_source)

    def test_local_plugin_package_channel_is_strict_and_version_agnostic(self):
        update_source = (ROOT / "plugin/src/core/update_manager.cpp").read_text(encoding="utf-8")
        settings_source = (ROOT / "plugin/src/editor/settings_panel.cpp").read_text(encoding="utf-8")
        select_local = update_source.split("Dictionary UpdateManager::select_local_package", 1)[1].split(
            "String UpdateManager::get_update_cache_root", 1
        )[0]

        self.assertIn('archive_path.contains("\\\\")', update_source)
        self.assertIn("canonical_path.is_absolute_path()", update_source)
        self.assertIn('parts[i] == ".."', update_source)
        self.assertIn("seen.has(path_key)", update_source)
        self.assertIn('relative == "plugin.cfg"', update_source)
        self.assertIn('relative == "godot-minigame.gdextension"', update_source)
        self.assertIn('relative == "update_helper.gd"', update_source)
        self.assertIn("platform_native_extension()", update_source)
        self.assertIn("source_sha256_before", select_local)
        self.assertIn("source_sha256_after", select_local)
        self.assertIn("cached_sha256", select_local)
        self.assertIn('remote_version_info["channel"] = "local"', select_local)
        self.assertIn("set_state(STATE_DOWNLOADED)", select_local)
        self.assertNotIn("is_newer_than", select_local)
        self.assertIn("remote.is_newer_than(local)", update_source)
        self.assertIn("clear_pending_update()", settings_source)

    def test_local_plugin_channel_persists_only_the_channel(self):
        settings_source = (ROOT / "plugin/src/editor/settings_panel.cpp").read_text(encoding="utf-8")
        self.assertIn('godot_minigame/plugin_update/channel', settings_source)
        self.assertIn('String("local") : String("remote")', settings_source)
        self.assertIn("EditorFileDialog::FILE_MODE_OPEN_FILE", settings_source)
        self.assertIn('add_filter("*.zip"', settings_source)
        self.assertIn('String::utf8("远端稳定版")', settings_source)
        self.assertIn('String::utf8("本地插件包")', settings_source)
        self.assertIn('String::utf8("升级")', settings_source)
        self.assertIn('String::utf8("同版本覆盖")', settings_source)
        self.assertIn('String::utf8("降级")', settings_source)
        persisted = settings_source.split("void SettingsPanel::save_plugin_update_channel", 1)[1].split(
            "void SettingsPanel::refresh_plugin_update_channel", 1
        )[0]
        self.assertNotIn("path", persisted.lower())

    def test_settings_panel_layout_is_scrollable_and_responsive(self):
        settings_source = (ROOT / "plugin/src/editor/settings_panel.cpp").read_text(encoding="utf-8")
        dock_source = (ROOT / "plugin/src/editor/toolkit_dock.cpp").read_text(encoding="utf-8")
        self.assertIn("ScrollContainer::SCROLL_MODE_DISABLED", settings_source)
        self.assertIn("ScrollContainer::SCROLL_MODE_AUTO", settings_source)
        self.assertGreaterEqual(settings_source.count("HFlowContainer"), 4)
        self.assertGreaterEqual(settings_source.count("GridContainer"), 2)
        self.assertIn("TextServer::AUTOWRAP_WORD_SMART", settings_source)
        self.assertNotIn("TabContainer", dock_source)
        self.assertNotIn('add_theme_constant_override("margin_left", -4)', dock_source)

    def test_promote_template_updates_catalog_and_projection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            catalog_path = root / "templates.json"
            versions_path = root / "versions.yaml"
            catalog = {
                "schema_version": 1,
                "templates": [
                    {
                        "godot_major": "godot4",
                        "godot_version": "4.5.2",
                        "tag": "4.5.2-test-r1",
                        "file": "minigame4.5.2-r1.tpz",
                        "sha256": "1" * 64,
                        "minimum_plugin": "1.0.4",
                        "source_branch": "4.5",
                        "source_commit": "a" * 40,
                        "status": "stable",
                    }
                ],
            }
            catalog_path.write_text(json.dumps(catalog), encoding="utf-8")
            versions_path.write_text(product.render_versions(catalog), encoding="utf-8")
            args = argparse.Namespace(
                catalog=catalog_path,
                versions=versions_path,
                godot_major="godot4",
                godot_version="4.5.2",
                tag="4.5.2-test-r2",
                file="minigame4.5.2-r2.tpz",
                sha256="2" * 64,
                minimum_plugin="1.0.4",
                source_branch="4.5",
                source_commit="b" * 40,
                status="stable",
            )
            product.command_promote_template(args)
            promoted = product.load_json(catalog_path)["templates"][0]
            self.assertEqual(promoted["tag"], "4.5.2-test-r2")
            self.assertIn("tag: 4.5.2-test-r2", versions_path.read_text(encoding="utf-8"))

    def test_plugin_zip_has_installable_addon_prefix(self):
        version = product.load_json(ROOT / "plugin/plugin.json")["version"]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            args = argparse.Namespace(
                root=ROOT,
                output_dir=output,
                native_dir=output / "native",
                staging_dir=output / "staging",
                require_binaries=False,
                bundle_template=[],
            )
            product.command_package_plugin(args)
            archive = output / f"godot-minigame-plugin-{version}.zip"
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
            self.assertIn("addons/godot-minigame/plugin.cfg", names)
            self.assertIn("addons/godot-minigame/update_helper.gd", names)
            self.assertTrue(all(name.startswith("addons/godot-minigame/") for name in names))
            self.assertFalse(any(name.endswith((".lib", ".exp")) for name in names))

    def test_plugin_zip_versions_native_libraries_and_descriptor(self):
        version = product.load_json(ROOT / "plugin/plugin.json")["version"]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            native = output / "native"
            libraries = {
                "windows/godot-minigame.windows.x86_64.dll": b"windows",
                "windows/godot-minigame.windows.editor.x86_64.dll": b"stale",
                "linux/libgodot-minigame.linux.x86_64.so": b"linux",
                "macos/libgodot-minigame.macos.dylib": b"macos",
            }
            for relative, content in libraries.items():
                path = native / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)

            args = argparse.Namespace(
                root=ROOT,
                output_dir=output / "package",
                native_dir=native,
                staging_dir=output / "staging",
                require_binaries=True,
                bundle_template=[],
            )
            product.command_package_plugin(args)
            archive = output / "package" / f"godot-minigame-plugin-{version}.zip"
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
                descriptor = package.read(
                    "addons/godot-minigame/godot-minigame.gdextension"
                ).decode("utf-8")

            expected = {
                f"addons/godot-minigame/bin/windows/godot-minigame.windows.x86_64.{version}.dll",
                f"addons/godot-minigame/bin/linux/libgodot-minigame.linux.x86_64.{version}.so",
                f"addons/godot-minigame/bin/macos/libgodot-minigame.macos.{version}.dylib",
            }
            self.assertTrue(expected.issubset(names))
            self.assertFalse(any("windows.editor.x86_64" in name for name in names))
            self.assertNotIn("godot-minigame.windows.x86_64.dll\"", descriptor)
            self.assertIn(f"godot-minigame.windows.x86_64.{version}.dll", descriptor)
            self.assertIn(f"libgodot-minigame.linux.x86_64.{version}.so", descriptor)
            self.assertIn(f"libgodot-minigame.macos.{version}.dylib", descriptor)

    def test_plugin_zip_can_bundle_template_once(self):
        version = product.load_json(ROOT / "plugin/plugin.json")["version"]
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            template = temporary_path / "minigame4.5.2-glx-2d-r1.tpz"
            template.write_bytes(b"template")
            output = temporary_path / "output"
            args = argparse.Namespace(
                root=ROOT,
                output_dir=output,
                native_dir=output / "native",
                staging_dir=output / "staging",
                require_binaries=False,
                bundle_template=[template],
            )
            product.command_package_plugin(args)
            archive = output / f"godot-minigame-plugin-{version}.zip"
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
                bundled = "addons/godot-minigame/resources/templates/minigame4.5.2-glx-2d-r1.tpz"
                self.assertEqual(names.count(bundled), 1)
                self.assertEqual(package.read(bundled), b"template")


if __name__ == "__main__":
    unittest.main()
