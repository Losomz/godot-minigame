from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SKILL_ROOT = SCRIPT_DIR.parent
DEFAULT_BUNDLE = "godot-4.6.2-rc-a16e481cf4"


def run(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True)


def git_output(target_repo: Path, *args: str) -> str:
    result = run(["git", *args], target_repo)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout.strip()


def load_manifest(bundle: str) -> tuple[Path, dict]:
    manifest_path = SKILL_ROOT / "patches" / bundle / "manifest.json"
    with manifest_path.open("r", encoding="utf-8") as fh:
        return manifest_path, json.load(fh)


def ensure_git_repo(target_repo: Path) -> None:
    if not (target_repo / ".git").exists():
        raise RuntimeError(f"Target is not a git repo: {target_repo}")


def ensure_repo_state(target_repo: Path, manifest: dict, allow_dirty: bool, allow_base_mismatch: bool) -> None:
    ensure_git_repo(target_repo)
    status = git_output(target_repo, "status", "--porcelain")
    if status and not allow_dirty:
        raise RuntimeError(
            "Target repo is dirty. Commit/stash changes first or pass --allow-dirty."
        )

    try:
        head = git_output(target_repo, "rev-parse", "HEAD")
    except RuntimeError as exc:
        raise RuntimeError(
            "Target repo does not have a valid HEAD yet. "
            "Clone or checkout the official Godot base first."
        ) from exc
    if head != manifest["base_ref"] and not allow_base_mismatch:
        raise RuntimeError(
            "Target HEAD does not match the supported base.\n"
            f"Expected: {manifest['base_ref']}\n"
            f"Actual:   {head}\n"
            "Use --allow-base-mismatch only if you are intentionally doing a near-base refresh."
        )


def copy_tree_contents(src_root: Path, dst_root: Path) -> None:
    for path in src_root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src_root)
        if rel.parts and rel.parts[0] == "optional":
            continue
        dst = dst_root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dst)


def copy_optional_files(src_root: Path, dst_root: Path, rel_paths: list[str]) -> None:
    for rel in rel_paths:
        src = src_root / rel
        dst = dst_root / Path(rel.replace("optional/", "", 1))
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def apply_patch(target_repo: Path, patch_path: Path) -> None:
    check = run(["git", "apply", "--check", str(patch_path)], target_repo)
    if check.returncode != 0:
        raise RuntimeError(
            f"Patch check failed: {patch_path}\n{check.stderr.strip() or check.stdout.strip()}"
        )
    apply = run(["git", "apply", str(patch_path)], target_repo)
    if apply.returncode != 0:
        raise RuntimeError(
            f"Patch apply failed: {patch_path}\n{apply.stderr.strip() or apply.stdout.strip()}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply the bundled Godot WeChat Mini Game patch/source kit to an official checkout."
    )
    parser.add_argument("target_repo", help="Path to the target Godot repo root")
    parser.add_argument("--bundle", default=DEFAULT_BUNDLE, help="Bundle id to apply")
    parser.add_argument(
        "--include-optional",
        action="append",
        default=[],
        choices=["audio-worker", "dev-types"],
        help="Optional feature to include. May be passed multiple times.",
    )
    parser.add_argument(
        "--allow-dirty",
        action="store_true",
        help="Allow applying onto a dirty repo.",
    )
    parser.add_argument(
        "--allow-base-mismatch",
        action="store_true",
        help="Allow applying onto a repo that is not exactly on the supported base commit.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    target_repo = Path(args.target_repo).resolve()
    manifest_path, manifest = load_manifest(args.bundle)

    try:
        ensure_repo_state(target_repo, manifest, args.allow_dirty, args.allow_base_mismatch)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    patch_root = manifest_path.parent
    source_root = SKILL_ROOT / "sources" / args.bundle

    copy_tree_contents(source_root, target_repo)

    for rel_patch in manifest["core_patch_series"]:
        apply_patch(target_repo, patch_root / rel_patch)

    for feature in args.include_optional:
        feature_meta = manifest["optional_features"][feature]
        copy_optional_files(source_root, target_repo, feature_meta["copy"])
        for rel_patch in feature_meta.get("patches", []):
            apply_patch(target_repo, patch_root / rel_patch)

    print(f"Applied bundle: {args.bundle}")
    print(f"Target repo: {target_repo}")
    print(f"Base ref: {manifest['base_ref']}")
    if args.include_optional:
        print("Optional features: " + ", ".join(args.include_optional))
    else:
        print("Optional features: none")
    print("Next steps:")
    print("  1. Build with: scons platform=web target=template_release threads=no wasm_simd=no")
    print(f"  2. Post-process with: node {SKILL_ROOT / 'scripts' / 'godot_process.js'}")
    print(f"  3. Compress wasm with: cmd /c {SKILL_ROOT / 'scripts' / 'compress_wasm.bat'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
