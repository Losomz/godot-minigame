from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[2]
GODOT_BIN = os.environ.get("GODOT_BIN")
NATIVE_DLL = ROOT / "dist/plugin/native/windows/godot-minigame.windows.x86_64.dll"
RUNTIME_SCRIPT = ROOT / "tests/plugin/local_package_runtime.gd"
PREFIX = "addons/godot-minigame/"


def package_entries(version: str) -> dict[str, bytes]:
    return {
        PREFIX + "plugin.cfg": f'[plugin]\nversion="{version}"\n'.encode(),
        PREFIX + "godot-minigame.gdextension": b"[configuration]\n",
        PREFIX + "update_helper.gd": b"extends SceneTree\n",
        PREFIX + "bin/windows/godot-minigame.windows.x86_64.dll": b"native",
    }


def write_package(path: Path, entries: list[tuple[str, bytes]]) -> None:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", UserWarning)
        with zipfile.ZipFile(path, "w") as package:
            for name, content in entries:
                package.writestr(name, content)


@unittest.skipUnless(GODOT_BIN and NATIVE_DLL.is_file(), "set GODOT_BIN after building the Windows plugin")
class LocalPackageRuntimeTests(unittest.TestCase):
    def test_real_update_manager_validates_caches_and_cleans_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "project"
            addon = project / "addons/godot-minigame"
            shutil.copytree(ROOT / "plugin/addon", addon)
            native = addon / "bin/windows/godot-minigame.windows.x86_64.dll"
            native.parent.mkdir(parents=True)
            shutil.copy2(NATIVE_DLL, native)
            (project / "project.godot").write_text(
                'config_version=5\n[application]\nconfig/name="Local Package Test"\n',
                encoding="utf-8",
            )
            metadata = project / ".godot"
            metadata.mkdir()
            (metadata / "extension_list.cfg").write_text(
                "res://addons/godot-minigame/godot-minigame.gdextension\n",
                encoding="utf-8",
            )

            fixtures = root / "fixtures"
            fixtures.mkdir()
            cases: list[dict[str, object]] = []

            for name, version in (("same", "1.0.9"), ("newer", "1.1.0"), ("older", "1.0.8")):
                path = fixtures / f"{name}.zip"
                write_package(path, list(package_entries(version).items()))
                cases.append({"name": name, "path": str(path), "valid": True, "version": version})

            invalid_entries = {
                "outside": list(package_entries("1.0.9").items()) + [("outside.txt", b"bad")],
                "traversal": list(package_entries("1.0.9").items()) + [(PREFIX + "../bad.txt", b"bad")],
                "backslash": list(package_entries("1.0.9").items()),
                "duplicate": list(package_entries("1.0.9").items()) + [(PREFIX + "plugin.cfg", b"duplicate")],
                "missing-config": [(name, data) for name, data in package_entries("1.0.9").items() if not name.endswith("plugin.cfg")],
                "missing-descriptor": [(name, data) for name, data in package_entries("1.0.9").items() if not name.endswith(".gdextension")],
                "missing-helper": [(name, data) for name, data in package_entries("1.0.9").items() if not name.endswith("update_helper.gd")],
                "missing-native": [(name, data) for name, data in package_entries("1.0.9").items() if not name.endswith(".dll")],
            }
            for name, entries in invalid_entries.items():
                path = fixtures / f"{name}.zip"
                write_package(path, entries)
                if name == "backslash":
                    path.write_bytes(path.read_bytes().replace(b"addons/godot-minigame", b"addons\\godot-minigame"))
                cases.append({"name": name, "path": str(path), "valid": False})

            manifest = root / "fixtures.json"
            manifest.write_text(json.dumps(cases), encoding="utf-8")
            command = [
                str(GODOT_BIN),
                "--headless",
                "--editor",
                "--path",
                str(project),
                "--script",
                str(RUNTIME_SCRIPT),
                "--quit-after",
                "300",
                "--",
                str(manifest),
            ]
            completed = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertNotIn("LOCAL_PACKAGE_TEST_FAILED", completed.stdout)
            self.assertIn("Local plugin package runtime tests passed", completed.stdout)
            windows_headless_shutdown = os.name == "nt" and completed.returncode == 0xC0000374
            self.assertTrue(
                completed.returncode == 0 or windows_headless_shutdown,
                completed.stdout + completed.stderr,
            )


if __name__ == "__main__":
    unittest.main()
