from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
SKILL_ROOT = SCRIPT_DIR.parent
REPOSITORY_ROOT = SKILL_ROOT.parent


def run(
    cmd: list[str], cwd: Path, input_text: str | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, input=input_text, capture_output=True)


def run_patch(cmd: list[str], cwd: Path, patch_path: Path) -> subprocess.CompletedProcess[bytes]:
    patch_bytes = patch_path.read_bytes().replace(b"\r\n", b"\n")
    return subprocess.run(cmd, cwd=cwd, input=patch_bytes, capture_output=True)


def process_error(result: subprocess.CompletedProcess) -> str:
    output = result.stderr or result.stdout
    if isinstance(output, bytes):
        return output.decode("utf-8", errors="replace").strip()
    return output.strip()


def git_output(target_repo: Path, *args: str) -> str:
    result = run(["git", *args], target_repo)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result.stdout.strip()


def load_manifest() -> tuple[Path, dict]:
    manifest_path = SKILL_ROOT / "patches" / "manifest.json"
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


def copy_adapter_paths(adapter_root: Path, dst_root: Path, entries: list[dict]) -> None:
    for entry in entries:
        src = adapter_root / entry["source"]
        dst = dst_root / entry["destination"]
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True)
        elif src.is_file():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        else:
            raise RuntimeError(f"Adapter copy source does not exist: {src}")


def apply_patch(target_repo: Path, patch_path: Path) -> None:
    check = run_patch(["git", "apply", "--check", "-"], target_repo, patch_path)
    if check.returncode != 0:
        raise RuntimeError(
            f"Patch check failed: {patch_path}\n{process_error(check)}"
        )
    apply = run_patch(["git", "apply", "-"], target_repo, patch_path)
    if apply.returncode != 0:
        raise RuntimeError(
            f"Patch apply failed: {patch_path}\n{process_error(apply)}"
        )


def check_patch(target_repo: Path, patch_path: Path) -> None:
    check = run_patch(["git", "apply", "--check", "-"], target_repo, patch_path)
    if check.returncode != 0:
        raise RuntimeError(
            f"Patch check failed: {patch_path}\n{process_error(check)}"
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply the bundled Godot WeChat Mini Game patch/source kit to an official checkout."
    )
    parser.add_argument(
        "target_repo",
        nargs="?",
        default=str(SKILL_ROOT / "thirdparty" / "godot"),
        help="Path to the target Godot repo root (default: adapter/thirdparty/godot submodule)",
    )
    parser.add_argument(
        "--include-optional",
        action="append",
        default=[],
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
    manifest_path, manifest = load_manifest()

    try:
        ensure_repo_state(target_repo, manifest, args.allow_dirty, args.allow_base_mismatch)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    patch_root = manifest_path.parent
    source_root = SKILL_ROOT / "sources"

    unknown_features = sorted(set(args.include_optional) - set(manifest["optional_features"]))
    if unknown_features:
        available = ", ".join(sorted(manifest["optional_features"])) or "none"
        print(
            f"Unknown optional feature(s): {', '.join(unknown_features)}\n"
            f"Available: {available}",
            file=sys.stderr,
        )
        return 1

    selected_patches = [patch_root / rel for rel in manifest["core_patch_series"]]
    for feature in args.include_optional:
        selected_patches.extend(
            patch_root / rel for rel in manifest["optional_features"][feature].get("patches", [])
        )

    try:
        for patch_path in selected_patches:
            check_patch(target_repo, patch_path)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    copy_tree_contents(source_root, target_repo)

    for rel_patch in manifest["core_patch_series"]:
        apply_patch(target_repo, patch_root / rel_patch)

    for feature in args.include_optional:
        feature_meta = manifest["optional_features"][feature]
        copy_optional_files(source_root, target_repo, feature_meta["copy"])
        copy_adapter_paths(SKILL_ROOT, target_repo, feature_meta.get("adapter_copy", []))
        for rel_patch in feature_meta.get("patches", []):
            apply_patch(target_repo, patch_root / rel_patch)

    print("Applied adapter baseline")
    print(f"Target repo: {target_repo}")
    print(f"Base ref: {manifest['base_ref']}")
    if args.include_optional:
        print("Optional features: " + ", ".join(args.include_optional))
    else:
        print("Optional features: none")
    print("Next steps:")
    next_steps = manifest.get("next_steps") or [
        "Build with: scons platform=web target=template_release threads=no wasm_simd=no",
        f"Post-process with: node {SKILL_ROOT / 'scripts' / 'godot_process.js'}",
        f"Compress wasm with: cmd /c {SKILL_ROOT / 'scripts' / 'compress_wasm.bat'}",
    ]
    for index, step in enumerate(next_steps, start=1):
        print(f"  {index}. {step}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
