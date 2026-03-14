from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Copy the minimal WeChat runtime files from this skill into a target directory.")
    parser.add_argument("dest", help="Destination directory")
    parser.add_argument("--force", action="store_true", help="Overwrite existing files")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    asset_dir = script_dir.parent / "assets" / "min-runtime"
    dest_dir = Path(args.dest).resolve()
    dest_dir.mkdir(parents=True, exist_ok=True)

    copied: list[Path] = []
    for name in ("godot-sdk.js", "godot-loader.js"):
        src = asset_dir / name
        dst = dest_dir / name

        if dst.exists() and not args.force:
            raise SystemExit(f"{dst} already exists; rerun with --force to overwrite")

        shutil.copy2(src, dst)
        copied.append(dst)

    print("Copied minimal runtime files:")
    for path in copied:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
