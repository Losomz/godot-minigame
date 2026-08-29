"""Unified packaging entry point.

Wraps adapter/scripts/package_wechat_glx_template.py with template-base
selection (adapter/templates/manifest.json), trim-profile short names, engine
variants, exception mode, ad merging and output directory.

Examples:
    python adapter/ci/package.py --list
    python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --exceptions enabled --revision 1
    python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --exceptions disabled --revision 2 --ad
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TEMPLATES_MANIFEST_PATH = REPO_ROOT / "adapter" / "templates" / "manifest.json"
CONFIGS_DIR = REPO_ROOT / "adapter" / "configs"
PACKAGE_SCRIPT = REPO_ROOT / "adapter" / "scripts" / "package_wechat_glx_template.py"


def list_templates() -> None:
    manifest = json.loads(TEMPLATES_MANIFEST_PATH.read_text(encoding="utf-8"))
    print("可用模板（打包基底，adapter/templates/manifest.json）:")
    if not manifest:
        print("  (无)")
    for template_id, entry in sorted(manifest.items()):
        print(f"  {template_id}: {entry['dir']}  engine={entry.get('engine', '?')}  "
              f"默认裁切={entry.get('default_profile', '-')}")

    print("\n可用裁切模板（adapter/configs/）:")
    profiles = sorted(CONFIGS_DIR.glob("*.py")) if CONFIGS_DIR.is_dir() else []
    if not profiles:
        print("  (无)")
    for path in profiles:
        short = path.stem.removeprefix("wechat_")
        print(f"  {short:<14} -> {path.relative_to(REPO_ROOT).as_posix()}")

    print("\n变体维度:")
    print("  --variant     glx | webgl（默认 glx；webgl 运行壳暂未提供）")
    print("  --exceptions  enabled | disabled（默认 enabled；disabled 省 ~1.14 MiB）")
    print("  --ad          融合微信广告组件（adapter/wechat_ad）")

    print("\n示例:")
    print("  python adapter/ci/package.py --template 4.5.2 --variant glx --profile 2d --revision 1")
    print("  python adapter/ci/package.py --template 4.5.2 --variant glx --profile my_trim.py "
          "--exceptions disabled --revision 2 --ad")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="统一打包入口：模板基底 + 裁切模板 + 变体组合 -> dist/",
    )
    parser.add_argument("--list", action="store_true", help="列出可用模板与裁切模板")
    parser.add_argument("--template", default="4.5.2", help="模板基底 id（adapter/templates/manifest.json）")
    parser.add_argument("--variant", default="glx", choices=["glx", "webgl"], help="引擎变体（默认 glx）")
    parser.add_argument("--profile", default=None,
                        help="裁切模板：简名（如 2d）或 .py 文件路径；默认取模板登记表的 default_profile")
    parser.add_argument("--exceptions", default="enabled", choices=["enabled", "disabled"])
    parser.add_argument("--ad", action="store_true", help="融合微信广告组件")
    parser.add_argument("--revision", type=int, default=1, help="不可变产物版本号")
    parser.add_argument("--incremental", action="store_true", help="跳过 SCons clean")
    parser.add_argument("--out", default="dist", help="输出目录（默认 dist/）")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list:
        list_templates()
        return 0

    manifest = json.loads(TEMPLATES_MANIFEST_PATH.read_text(encoding="utf-8"))
    entry = manifest.get(args.template)
    if not entry:
        print(f"error: unknown template {args.template!r}; "
              f"available: {', '.join(sorted(manifest)) or '(none)'}", file=sys.stderr)
        return 2

    profile = args.profile or entry.get("default_profile")
    if not profile:
        print(f"error: template {args.template!r} has no default_profile and --profile was not given",
              file=sys.stderr)
        return 2

    cmd = [
        sys.executable,
        str(PACKAGE_SCRIPT),
        "--template", args.template,
        "--variant", args.variant,
        "--profile", profile,
        "--exceptions", args.exceptions,
        "--revision", str(args.revision),
        "--out", args.out,
    ]
    if args.ad:
        cmd.append("--ad")
    if args.incremental:
        cmd.append("--incremental")

    print(f"==> {cmd[0]} {' '.join(cmd[1:])}")
    return subprocess.call(cmd)


if __name__ == "__main__":
    raise SystemExit(main())
