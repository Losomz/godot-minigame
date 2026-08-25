#!/usr/bin/env python3
"""Validate and package the Godot Minigame product control plane."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SEMVER_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
TEMPLATE_TAG_RE = re.compile(r"^template-([0-9]+\.[0-9]+\.[0-9]+)-r([1-9][0-9]*)$")


class ProductError(RuntimeError):
    pass


def load_json(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        raise ProductError(f"Cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ProductError(f"JSON root must be an object: {path}")
    return value


def write_json_atomic(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False) + "\n"
    write_text_atomic(path, payload)


def write_text_atomic(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(payload)
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def parse_semver(value: str, label: str) -> tuple[int, int, int]:
    if not isinstance(value, str) or not SEMVER_RE.fullmatch(value):
        raise ProductError(f"{label} must be a semantic version: {value!r}")
    core = value.split("-", 1)[0]
    return tuple(int(part) for part in core.split("."))


def read_plugin_cfg_version(path: Path) -> str:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
        return parser["plugin"]["version"].strip().strip('"')
    except (OSError, KeyError, configparser.Error) as exc:
        raise ProductError(f"Cannot read plugin version from {path}: {exc}") from exc


def version_key(value: str) -> tuple[int, int, int]:
    return parse_semver(value, "Godot version")


def render_versions(catalog: dict) -> str:
    entries = catalog.get("templates")
    if not isinstance(entries, list):
        raise ProductError("catalog/templates.json must contain a templates array")

    groups: dict[str, list[dict]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ProductError("Every template entry must be an object")
        major = entry.get("godot_major")
        if not isinstance(major, str) or not re.fullmatch(r"godot[0-9]+", major):
            raise ProductError(f"Invalid godot_major: {major!r}")
        groups.setdefault(major, []).append(entry)

    lines: list[str] = []
    for major in sorted(groups, key=lambda item: int(item[5:])):
        lines.append(f"{major}:")
        for entry in sorted(groups[major], key=lambda item: version_key(item["godot_version"])):
            version = entry["godot_version"]
            filename = entry["file"]
            tag = entry.get("tag")
            if not tag:
                lines.append(f"  {version}: {filename}")
                continue
            lines.extend((f"  {version}:", f"    tag: {tag}", f"    file: {filename}"))
    return "\n".join(lines) + "\n"


def validate_plugin(root: Path, plugin: dict, update_catalog: dict) -> list[str]:
    errors: list[str] = []
    try:
        version = plugin["version"]
        parse_semver(version, "product/plugin.json version")
    except (KeyError, ProductError) as exc:
        return [str(exc)]

    if plugin.get("schema_version") != 1:
        errors.append("product/plugin.json schema_version must be 1")
    if plugin.get("tag_prefix") != "plugin-v":
        errors.append("Plugin tag_prefix must be plugin-v")

    addon_path = root / str(plugin.get("addon_path", ""))
    if not addon_path.is_dir():
        errors.append(f"Plugin addon_path does not exist: {addon_path}")

    for cfg in (root / "plugin.cfg", addon_path / "plugin.cfg"):
        try:
            cfg_version = read_plugin_cfg_version(cfg)
            if cfg_version != version:
                errors.append(f"Plugin version drift: {cfg} has {cfg_version}, expected {version}")
        except ProductError as exc:
            errors.append(str(exc))

    if update_catalog.get("schema_version") != 1:
        errors.append("catalog/plugin-stable.json schema_version must be 1")
    if update_catalog.get("version") != version:
        errors.append("Plugin update catalog version must match product/plugin.json")

    published = update_catalog.get("published")
    if not isinstance(published, bool):
        errors.append("Plugin update catalog published must be boolean")
    if published:
        expected_tag = f"plugin-v{version}"
        if update_catalog.get("tag") != expected_tag:
            errors.append(f"Published plugin tag must be {expected_tag}")
        platforms = update_catalog.get("platforms")
        if not isinstance(platforms, dict) or not platforms:
            errors.append("Published plugin catalog must contain platform assets")
        else:
            for platform, asset in platforms.items():
                if not isinstance(asset, dict):
                    errors.append(f"Plugin platform {platform} must be an object")
                    continue
                if not str(asset.get("url", "")).startswith("https://"):
                    errors.append(f"Plugin platform {platform} must have an HTTPS URL")
                if not SHA256_RE.fullmatch(str(asset.get("sha256", ""))):
                    errors.append(f"Plugin platform {platform} must have a SHA-256 digest")
                if not str(asset.get("asset", "")).endswith(".zip"):
                    errors.append(f"Plugin platform {platform} asset must be a zip")
    elif update_catalog.get("tag") or update_catalog.get("platforms"):
        errors.append("Unpublished plugin catalog must not expose a tag or platform assets")

    return errors


def validate_adapters(root: Path, adapters: dict) -> list[str]:
    errors: list[str] = []
    if adapters.get("schema_version") != 1:
        errors.append("product/adapters.json schema_version must be 1")
    entries = adapters.get("adapters")
    if not isinstance(entries, list) or not entries:
        return errors + ["product/adapters.json must contain at least one adapter"]

    ids: set[str] = set()
    branches: set[str] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("Every adapter entry must be an object")
            continue
        adapter_id = str(entry.get("id", ""))
        branch = str(entry.get("branch", ""))
        if not adapter_id or adapter_id in ids:
            errors.append(f"Adapter id is empty or duplicated: {adapter_id!r}")
        if not branch or branch in branches:
            errors.append(f"Adapter branch is empty or duplicated: {branch!r}")
        if branch in {"main", "develop", "upstream-sync"}:
            errors.append(f"Adapter {adapter_id} must use a version-specific template branch")
        ids.add(adapter_id)
        branches.add(branch)
        try:
            parse_semver(str(entry.get("godot_version", "")), f"Adapter {adapter_id} Godot version")
        except ProductError as exc:
            errors.append(str(exc))
        if entry.get("status") not in {"experimental", "stable", "maintenance", "retired"}:
            errors.append(f"Adapter {adapter_id} has an invalid status")
        workflow = str(entry.get("workflow", ""))
        if Path(workflow).name != workflow or not (root / ".github/workflows" / workflow).is_file():
            errors.append(f"Adapter {adapter_id} workflow does not exist: {workflow!r}")
        required_paths = entry.get("required_paths")
        if not isinstance(required_paths, list) or not all(isinstance(item, str) and item for item in required_paths):
            errors.append(f"Adapter {adapter_id} required_paths must be non-empty strings")
    return errors


def validate_templates(catalog: dict, adapters: dict) -> list[str]:
    errors: list[str] = []
    if catalog.get("schema_version") != 1:
        errors.append("catalog/templates.json schema_version must be 1")
    entries = catalog.get("templates")
    if not isinstance(entries, list) or not entries:
        return errors + ["catalog/templates.json must contain templates"]

    seen: set[tuple[str, str]] = set()
    registered_sources = {
        (str(entry.get("godot_version", "")), str(entry.get("branch", "")))
        for entry in adapters.get("adapters", [])
        if isinstance(entry, dict)
    }
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("Every template entry must be an object")
            continue
        major = str(entry.get("godot_major", ""))
        version = str(entry.get("godot_version", ""))
        identity = (major, version)
        if identity in seen:
            errors.append(f"Duplicate template entry: {major}/{version}")
        seen.add(identity)
        if not re.fullmatch(r"godot[0-9]+", major):
            errors.append(f"Invalid template major: {major!r}")
        try:
            parse_semver(version, f"Template {major} version")
            parse_semver(str(entry.get("minimum_plugin", "")), f"Template {major}/{version} minimum_plugin")
        except ProductError as exc:
            errors.append(str(exc))
        filename = str(entry.get("file", ""))
        if not filename.endswith(".tpz") or Path(filename).name != filename:
            errors.append(f"Template {major}/{version} file must be a .tpz basename")
        status = entry.get("status")
        if status not in {"legacy", "prerelease", "stable", "retired"}:
            errors.append(f"Template {major}/{version} has an invalid status")
        if not str(entry.get("source_branch", "")):
            errors.append(f"Template {major}/{version} must identify its source_branch")
        tag = entry.get("tag")
        if status != "legacy" and not TEMPLATE_TAG_RE.fullmatch(str(tag or "")):
            errors.append(f"Promoted template {major}/{version} must use a namespaced template tag")
        sha256 = entry.get("sha256")
        if status != "legacy" and not SHA256_RE.fullmatch(str(sha256 or "")):
            errors.append(f"Promoted template {major}/{version} must include SHA-256")
        source_commit = entry.get("source_commit")
        if status != "legacy" and not COMMIT_RE.fullmatch(str(source_commit or "")):
            errors.append(f"Promoted template {major}/{version} must include a full source commit")
        if status != "legacy" and (version, str(entry.get("source_branch", ""))) not in registered_sources:
            errors.append(f"Promoted template {major}/{version} must use a registered template source branch")
    return errors


def validate_repository(root: Path) -> None:
    plugin = load_json(root / "product/plugin.json")
    adapters = load_json(root / "product/adapters.json")
    update_catalog = load_json(root / "catalog/plugin-stable.json")
    template_catalog = load_json(root / "catalog/templates.json")

    errors = []
    errors.extend(validate_plugin(root, plugin, update_catalog))
    errors.extend(validate_adapters(root, adapters))
    errors.extend(validate_templates(template_catalog, adapters))

    expected_versions = render_versions(template_catalog)
    versions_path = root / "resources/versions.yaml"
    try:
        current_versions = versions_path.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"Cannot read {versions_path}: {exc}")
    else:
        if current_versions.replace("\r\n", "\n") != expected_versions:
            errors.append("resources/versions.yaml is stale; run render-versions")

    if errors:
        raise ProductError("Product validation failed:\n- " + "\n- ".join(errors))


def command_validate(args: argparse.Namespace) -> None:
    validate_repository(args.root)
    print("Product contracts are valid.")


def command_render_versions(args: argparse.Namespace) -> None:
    catalog = load_json(args.catalog)
    payload = render_versions(catalog)
    if args.check:
        current = args.output.read_text(encoding="utf-8").replace("\r\n", "\n")
        if current != payload:
            raise ProductError(f"Generated versions file is stale: {args.output}")
        print(f"Versions projection is current: {args.output}")
        return
    write_text_atomic(args.output, payload)
    print(f"Rendered {args.output}")


def next_template_revision(tag: str) -> int:
    match = TEMPLATE_TAG_RE.fullmatch(tag)
    return int(match.group(2)) if match else 0


def command_promote_template(args: argparse.Namespace) -> None:
    parse_semver(args.godot_version, "Godot version")
    parse_semver(args.minimum_plugin, "Minimum plugin version")
    expected_tag = re.compile(rf"^template-{re.escape(args.godot_version)}-r([1-9][0-9]*)$")
    match = expected_tag.fullmatch(args.tag)
    if not match:
        raise ProductError(f"Tag must match template-{args.godot_version}-rN")
    if not SHA256_RE.fullmatch(args.sha256):
        raise ProductError("Template promotion requires a 64-character SHA-256")
    if not COMMIT_RE.fullmatch(args.source_commit):
        raise ProductError("Template promotion requires a full lowercase source commit")
    if Path(args.file).name != args.file or not args.file.endswith(".tpz"):
        raise ProductError("Template asset must be a .tpz basename")

    catalog = load_json(args.catalog)
    entries = catalog.get("templates")
    if not isinstance(entries, list):
        raise ProductError("Template catalog has no templates array")

    incoming_revision = int(match.group(1))
    replacement = {
        "godot_major": args.godot_major,
        "godot_version": args.godot_version,
        "tag": args.tag,
        "file": args.file,
        "sha256": args.sha256.lower(),
        "minimum_plugin": args.minimum_plugin,
        "source_branch": args.source_branch,
        "source_commit": args.source_commit,
        "status": args.status,
    }

    replaced = False
    new_entries: list[dict] = []
    for entry in entries:
        if entry.get("godot_major") == args.godot_major and entry.get("godot_version") == args.godot_version:
            existing_revision = next_template_revision(str(entry.get("tag", "")))
            if existing_revision >= incoming_revision:
                raise ProductError(
                    f"Template revision must increase: existing r{existing_revision}, incoming r{incoming_revision}"
                )
            new_entries.append(replacement)
            replaced = True
        else:
            new_entries.append(entry)
    if not replaced:
        new_entries.append(replacement)
    catalog["templates"] = new_entries

    payload = render_versions(catalog)
    write_json_atomic(args.catalog, catalog)
    write_text_atomic(args.versions, payload)
    print(f"Promoted {args.tag} into {args.catalog}")


def iter_addon_files(addon_path: Path):
    for path in sorted(addon_path.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(addon_path)
        if any(part in {".godot", "__pycache__"} for part in relative.parts):
            continue
        if path.suffix in {".tmp", ".pyc"}:
            continue
        yield path, relative


def command_package_plugin(args: argparse.Namespace) -> None:
    plugin = load_json(args.root / "product/plugin.json")
    validate_repository(args.root)
    version = plugin["version"]
    addon_path = args.root / plugin["addon_path"]
    files = list(iter_addon_files(addon_path))
    if not files:
        raise ProductError(f"No plugin files found under {addon_path}")
    if args.require_binaries and not any(path.suffix.lower() in {".dll", ".so", ".dylib"} for path, _ in files):
        raise ProductError("Plugin package requires native binaries, but none were found")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    archive = args.output_dir / f"godot-minigame-plugin-{version}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for source, relative in files:
            archive_name = Path("addons/godot-minigame") / relative
            info = zipfile.ZipInfo(archive_name.as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            output.writestr(info, source.read_bytes())

    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    checksum.write_text(f"{digest}  {archive.name}\n", encoding="ascii", newline="\n")
    print(f"Created {archive}")
    print(f"SHA-256 {digest}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validate = subparsers.add_parser("validate", help="validate all product contracts")
    validate.set_defaults(func=command_validate)

    render = subparsers.add_parser("render-versions", help="render resources/versions.yaml from the catalog")
    render.add_argument("--catalog", type=Path, default=ROOT / "catalog/templates.json")
    render.add_argument("--output", type=Path, default=ROOT / "resources/versions.yaml")
    render.add_argument("--check", action="store_true")
    render.set_defaults(func=command_render_versions)

    promote = subparsers.add_parser("promote-template", help="promote a verified template into the product catalog")
    promote.add_argument("--catalog", type=Path, default=ROOT / "catalog/templates.json")
    promote.add_argument("--versions", type=Path, default=ROOT / "resources/versions.yaml")
    promote.add_argument("--godot-major", required=True)
    promote.add_argument("--godot-version", required=True)
    promote.add_argument("--tag", required=True)
    promote.add_argument("--file", required=True)
    promote.add_argument("--sha256", required=True)
    promote.add_argument("--minimum-plugin", required=True)
    promote.add_argument("--source-branch", required=True)
    promote.add_argument("--source-commit", required=True)
    promote.add_argument("--status", choices=("prerelease", "stable"), default="prerelease")
    promote.set_defaults(func=command_promote_template)

    package = subparsers.add_parser("package-plugin", help="create a deterministic installable plugin zip")
    package.add_argument("--output-dir", type=Path, default=ROOT / "dist/plugin")
    package.add_argument("--require-binaries", action="store_true")
    package.set_defaults(func=command_package_plugin)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    args.root = args.root.resolve()
    try:
        args.func(args)
    except (ProductError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
