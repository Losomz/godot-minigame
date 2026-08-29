from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RepositoryLayoutTests(unittest.TestCase):
    def test_top_level_ownership_is_explicit(self):
        for directory in ("adapter", "plugin", "demo", "tests", "tools", "docs"):
            self.assertTrue((ROOT / directory).is_dir(), directory)

        for obsolete in (
            "addons",
            "catalog",
            "ci",
            "include",
            "product",
            "resources",
            "src",
            "templates",
        ):
            self.assertFalse((ROOT / obsolete).exists(), obsolete)

    def test_plugin_is_self_contained(self):
        for relative in (
            "addon/plugin.cfg",
            "build/SConstruct",
            "catalog/plugin-stable.json",
            "catalog/templates.json",
            "include",
            "plugin.json",
            "resources",
            "src",
            "thirdparty/godot-cpp",
        ):
            self.assertTrue((ROOT / "plugin" / relative).exists(), relative)

    def test_adapter_uses_the_45_baseline(self):
        for relative in (
            "adapters.json",
            "ci/package.py",
            "configs/wechat_2d.py",
            "patches/manifest.json",
            "templates/manifest.json",
            "thirdparty/godot",
        ):
            self.assertTrue((ROOT / "adapter" / relative).exists(), relative)


if __name__ == "__main__":
    unittest.main()
