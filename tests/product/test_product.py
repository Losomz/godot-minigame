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
        catalog = product.load_json(ROOT / "catalog/templates.json")
        rendered = product.render_versions(catalog)
        current = (ROOT / "resources/versions.yaml").read_text(encoding="utf-8").replace("\r\n", "\n")
        self.assertEqual(rendered, current)

    def test_invalid_semver_is_rejected(self):
        with self.assertRaises(product.ProductError):
            product.parse_semver("1.2", "test version")

    def test_product_has_two_release_catalogs(self):
        plugin = product.load_json(ROOT / "product/plugin.json")
        adapters = product.load_json(ROOT / "product/adapters.json")
        self.assertEqual(plugin["update_catalog"], "catalog/plugin-stable.json")
        self.assertTrue(adapters["adapters"])
        for adapter in adapters["adapters"]:
            self.assertNotIn(adapter["branch"], {"main", "develop", "upstream-sync"})
            self.assertTrue((ROOT / ".github/workflows" / adapter["workflow"]).is_file())

    def test_staged_plugin_version_may_lead_published_stable_catalog(self):
        plugin = product.load_json(ROOT / "product/plugin.json")
        stable = product.load_json(ROOT / "catalog/plugin-stable.json")
        self.assertGreaterEqual(
            product.parse_semver(plugin["version"], "plugin"),
            product.parse_semver(stable["version"], "stable"),
        )
        self.assertEqual(stable["tag"], f"plugin-v{stable['version']}")

    def test_runtime_catalog_contract_and_legacy_preset_mapping_are_present(self):
        source = (ROOT / "src/templates/template_manager.cpp").read_text(encoding="utf-8")
        export_source = (ROOT / "src/editor/wechat_export_platform.cpp").read_text(encoding="utf-8")
        self.assertIn('status != "stable"', source)
        self.assertIn('minimum_plugin', source)
        self.assertIn('sha256', source)
        self.assertIn('templates-v1.json', source)
        self.assertIn('legacy_template_source == String::utf8("仅插件内模板")', export_source)
        self.assertIn('legacy_template_source == String::utf8("自动")', export_source)

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
            archive = output / "godot-minigame-plugin-1.0.5.zip"
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
            self.assertIn("addons/godot-minigame/plugin.cfg", names)
            self.assertTrue(all(name.startswith("addons/godot-minigame/") for name in names))
            self.assertFalse(any(name.endswith((".lib", ".exp")) for name in names))

    def test_plugin_zip_can_bundle_template_once(self):
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
            archive = output / "godot-minigame-plugin-1.0.5.zip"
            with zipfile.ZipFile(archive) as package:
                names = package.namelist()
                bundled = "addons/godot-minigame/resources/templates/minigame4.5.2-glx-2d-r1.tpz"
                self.assertEqual(names.count(bundled), 1)
                self.assertEqual(package.read(bundled), b"template")


if __name__ == "__main__":
    unittest.main()
