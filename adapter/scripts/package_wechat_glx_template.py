from __future__ import annotations

import argparse
import ast
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
GODOT_DIR = REPO_ROOT / "godot"
BUILD_DIR = GODOT_DIR / "bin" / ".web_zip"
TEMPLATE_DIR = REPO_ROOT / "templates" / "4.5.2" / "minigame4.5.2_glx"
PROFILE_PATH = REPO_ROOT / "adapter" / "configs" / "wechat_2d.py"
DIST_DIR = REPO_ROOT / "dist"
DIST_TEMPLATE_DIR = DIST_DIR / TEMPLATE_DIR.name
ARTIFACT_PREFIX = "minigame4.5.2-glx-2d"
RUNTIME_DIR = REPO_ROOT / "adapter" / "assets" / "min-runtime"
MANIFEST_PATH = REPO_ROOT / "adapter" / "patches" / "manifest.json"

GODOT_BASE = "6ce3de25aa58466e14ef354703ba8d9791a417da"
GLX_ALIGNMENT = "3dec2ca498f7b9e1ce07b33f5fbe08741a1429e5"
GLX_LIBRARY_SHA256 = "c70b6255285aa4e5b987f18cf24d59d56227c67d44d60b734755f33f6a8ab33a"
GLX_BRIDGE_SHA256 = "7f08edb7d3e7f3badc6c8b520b12c7424a9e52b223407ded92ae8f700bc01a0e"
BUILD_BASE_ARGUMENTS = [
    "platform=web",
    "target=template_release",
    "threads=no",
    "wasm_simd=no",
    "wechat_glx=yes",
]
BUILD_COMMAND = (
    f"scons {' '.join(BUILD_BASE_ARGUMENTS)} "
    "profile=adapter/configs/wechat_2d.py"
)

REQUIRED_EXPORTS = {
    "glxUpdateContextId",
    "glxInit",
    "glxInitBufferDataAndGlState",
    "glxCommandBufferFlush",
    "glxShowOpenData",
    "glxHideOpenData",
    "glxGetVideoTempBuffer",
    "glxVideoUpdateToTexture",
    "glxVideoDestroy",
}
REQUIRED_CALLBACKS = {
    "evalBatchRenderAsync",
    "evalBatchRenderSync",
    "evalBatchRenderSyncBool",
    "evalBatchRenderSyncInt",
    "evalBatchRenderSyncString",
    "evalBatchRenderArrayBuffer",
}
REQUIRED_RUNTIME_METHODS = {"ccall", "cwrap", "stringToUTF8", "lengthBytesUTF8"}
FORBIDDEN_IMPORTS = {
    "emscripten_glGenQueries",
    "emscripten_glDeleteQueries",
    "emscripten_webgl_commit_frame",
}
REQUIRED_ARCHIVE_ENTRIES = {
    "BUILD_INFO.md",
    "game.js",
    "game.json",
    "godot-loader.js",
    "engine/godot-sdk.js",
    "engine/godot.js",
    "engine/godot.wasm.br",
}


def resolve_command(command: list[str]) -> list[str]:
    executable = shutil.which(command[0])
    if executable is None:
        candidate = Path(command[0])
        if not candidate.is_file():
            raise RuntimeError(f"required command was not found: {command[0]}")
        executable = str(candidate.resolve())

    resolved = [executable, *command[1:]]
    if os.name == "nt" and Path(executable).suffix.lower() in {".bat", ".cmd"}:
        return ["cmd.exe", "/d", "/s", "/c", subprocess.list2cmdline(resolved)]
    return resolved


def run(command: list[str], cwd: Path = REPO_ROOT) -> str:
    resolved = resolve_command(command)
    result = subprocess.run(
        resolved,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"command failed ({' '.join(command)}):\n{detail}")
    return result.stdout.strip()


def run_visible(command: list[str], cwd: Path = REPO_ROOT) -> None:
    resolved = resolve_command(command)
    result = subprocess.run(resolved, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(f"command failed ({' '.join(command)})")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def read_profile_settings(path: Path) -> dict[str, bool]:
    require_file(path)
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    settings: dict[str, bool] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name):
            continue
        name = target.id
        is_profile_setting = (
            name.startswith("disable_")
            or name == "modules_enabled_by_default"
            or (name.startswith("module_") and name.endswith("_enabled"))
        )
        if not is_profile_setting:
            continue
        if not isinstance(statement.value, ast.Constant) or not isinstance(statement.value.value, bool):
            raise RuntimeError(f"build profile setting must be boolean: {name}")
        settings[name] = statement.value.value

    if not settings:
        raise RuntimeError(f"build profile contains no recognized settings: {path}")
    return dict(sorted(settings.items()))


def build_arguments(profile_path: Path) -> list[str]:
    return [*BUILD_BASE_ARGUMENTS, f"profile={profile_path.resolve()}"]


def normalized_lf(path: Path) -> bytes:
    return path.read_bytes().replace(b"\r\n", b"\n")


def require_file(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"required file is missing: {path}")


def verify_source() -> str:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    expected_ref = manifest.get("wechat_glx_ref")
    if not expected_ref:
        raise RuntimeError("manifest.json does not define wechat_glx_ref")

    status = run(["git", "status", "--porcelain", "--untracked-files=all"], GODOT_DIR)
    if status:
        raise RuntimeError("godot/ must be clean and committed before building")

    godot_commit = run(["git", "rev-parse", "HEAD"], GODOT_DIR)
    if godot_commit != expected_ref:
        raise RuntimeError(
            f"godot HEAD does not match manifest wechat_glx_ref: {godot_commit} != {expected_ref}"
        )
    run(["git", "merge-base", "--is-ancestor", GODOT_BASE, "HEAD"], GODOT_DIR)

    glx_patch = REPO_ROOT / "adapter" / "patches" / "optional" / "003-wechat-glx.patch"
    require_file(glx_patch)
    run(["git", "apply", "--reverse", "--check", str(glx_patch)], GODOT_DIR)

    synchronized_files = (
        (
            REPO_ROOT / "adapter" / "sources" / "godot_process.js",
            GODOT_DIR / "godot_process.js",
        ),
        (
            REPO_ROOT
            / "adapter"
            / "sources"
            / "platform"
            / "web"
            / "js"
            / "libs"
            / "library_godot_display.js",
            GODOT_DIR / "platform" / "web" / "js" / "libs" / "library_godot_display.js",
        ),
        (
            REPO_ROOT
            / "adapter"
            / "sources"
            / "optional"
            / "platform"
            / "web"
            / "js"
            / "patches"
            / "patch_em_gl.js",
            GODOT_DIR / "platform" / "web" / "js" / "patches" / "patch_em_gl.js",
        ),
    )
    for maintained, applied in synchronized_files:
        require_file(maintained)
        require_file(applied)
        if normalized_lf(maintained) != normalized_lf(applied):
            raise RuntimeError(f"maintained source differs from applied Godot file: {maintained}")

    library = GODOT_DIR / "thirdparty" / "wechat-glx" / "libemscriptenglx.a"
    bridge = GODOT_DIR / "platform" / "web" / "js" / "patches" / "patch_em_gl.js"
    require_file(library)
    if sha256(library) != GLX_LIBRARY_SHA256:
        raise RuntimeError("unexpected libemscriptenglx.a hash")
    if sha256(bridge) != GLX_BRIDGE_SHA256:
        raise RuntimeError("unexpected patch_em_gl.js hash")

    return godot_commit


def verify_tool_versions() -> tuple[str, str]:
    emcc = "emcc.bat" if os.name == "nt" else "emcc"
    emcc_version = run([emcc, "--version"]).splitlines()[0]
    if "4.0.10" not in emcc_version:
        raise RuntimeError(f"Emscripten 4.0.10 is required, got: {emcc_version}")

    brotli_version = run(["brotli", "--version"]).splitlines()[0]
    if "1.2.0" not in brotli_version:
        raise RuntimeError(f"Brotli 1.2.0 is required, got: {brotli_version}")

    run(["node", "--version"])
    return emcc_version, brotli_version


def build_engine(scons: str, incremental: bool, profile_path: Path) -> None:
    arguments = build_arguments(profile_path)
    if not incremental:
        run_visible([scons, "--clean", *arguments], GODOT_DIR)
    run_visible([scons, *arguments], GODOT_DIR)

    if os.name == "nt":
        run_visible(["cmd.exe", "/d", "/c", "compress_wasm.bat"], GODOT_DIR)
    else:
        run_visible(["sh", "./compress_wasm.sh"], GODOT_DIR)


def verify_build_environment(profile_settings: dict[str, bool]) -> dict[str, object]:
    env_path = GODOT_DIR / ".scons_env.json"
    require_file(env_path)
    build_env = json.loads(env_path.read_text(encoding="utf-8"))
    expected = {
        "platform": "web",
        "target": "template_release",
        "threads": False,
        "wasm_simd": False,
        "wechat_glx": True,
        "disable_exceptions": False,
        "arch": "wasm32",
    }
    mismatches = {
        key: (build_env.get(key), value)
        for key, value in expected.items()
        if build_env.get(key) != value
    }
    if mismatches:
        raise RuntimeError(f"unexpected SCons environment: {mismatches}")

    flags = {str(flag) for flag in build_env.get("LINKFLAGS", [])}
    required_flags = {
        "-sCHECK_NULL_WRITES=0",
        "-sERROR_ON_UNDEFINED_SYMBOLS=0",
        "-sSUPPORT_LONGJMP='emscripten'",
    }
    missing_flags = sorted(required_flags - flags)
    if missing_flags:
        raise RuntimeError(f"missing GLX link flags: {', '.join(missing_flags)}")
    if "-fexceptions" not in build_env.get("CXXFLAGS", []):
        raise RuntimeError("GLX build is missing -fexceptions")

    methods = set(build_env.get("EXPORTED_RUNTIME_METHODS", []))
    missing_methods = sorted(REQUIRED_RUNTIME_METHODS - methods)
    if missing_methods:
        raise RuntimeError(f"missing Emscripten runtime methods: {', '.join(missing_methods)}")
    if "#thirdparty/wechat-glx" not in build_env.get("LIBPATH", []):
        raise RuntimeError("GLX static library path is missing from the SCons environment")

    profile_mismatches = {
        name: (build_env.get(name), expected)
        for name, expected in profile_settings.items()
        if build_env.get(name) != expected
    }
    if profile_mismatches:
        details = ", ".join(
            f"{name}={actual!r} (expected {expected!r})"
            for name, (actual, expected) in profile_mismatches.items()
        )
        raise RuntimeError(f"build profile was not applied exactly: {details}")

    return build_env


def inspect_wasm(compressed_wasm: Path) -> dict[str, object]:
    script = r"""
const crypto = require("crypto");
const fs = require("fs");
const zlib = require("zlib");
const wasm = zlib.brotliDecompressSync(fs.readFileSync(process.argv[1]));
const module = new WebAssembly.Module(wasm);
console.log(JSON.stringify({
  sha256: crypto.createHash("sha256").update(wasm).digest("hex"),
  imports: WebAssembly.Module.imports(module).map((item) => item.name),
  exports: WebAssembly.Module.exports(module).map((item) => item.name),
}));
"""
    return json.loads(run(["node", "-e", script, str(compressed_wasm)]))


def verify_engine(engine_js: Path, compressed_wasm: Path, raw_wasm: Path) -> dict[str, object]:
    run(["node", "--check", str(engine_js)])

    content = engine_js.read_text(encoding="utf-8")
    exact_fragments = {
        "GLX runtime bridge": "function wxGLXGetNativeExport",
        "frame callback wrapper": "registerAfterDoFrame(function(){return Module._glxCommandBufferFlush();})",
    }
    for label, fragment in exact_fragments.items():
        if content.count(fragment) != 1:
            raise RuntimeError(f"{label} must occur exactly once in generated godot.js")
    if "__godotMinigameWXGLXEnabled" not in content:
        raise RuntimeError("generated godot.js is missing pinned context selection")

    for method in REQUIRED_RUNTIME_METHODS:
        if f'Module["{method}"]' not in content:
            raise RuntimeError(f"generated godot.js does not expose runtime method: {method}")
    for export_name in REQUIRED_EXPORTS:
        if f'Module["_{export_name}"]' not in content:
            raise RuntimeError(f"generated godot.js does not bind GLX export: {export_name}")

    forbidden_fragments = {
        "diagnostic injection": "WXGLX-DIAG",
        "generated query bypass": "__godotWXGLXQuery",
        "unused commit-frame shim": "var _emscripten_webgl_commit_frame=function(){};",
    }
    for label, fragment in forbidden_fragments.items():
        if fragment in content:
            raise RuntimeError(f"generated godot.js still contains {label}")

    with tempfile.TemporaryDirectory(prefix="godot-js-idempotence-") as temp:
        candidate = Path(temp) / "godot.js"
        shutil.copyfile(engine_js, candidate)
        run(["node", str(GODOT_DIR / "godot_process.js"), str(candidate)], GODOT_DIR)
        if candidate.read_bytes() != engine_js.read_bytes():
            raise RuntimeError("godot_process.js is not idempotent for the final generated JS")

    wasm = inspect_wasm(compressed_wasm)
    imports = set(wasm["imports"])
    exports = set(wasm["exports"])
    missing_exports = sorted(REQUIRED_EXPORTS - exports)
    missing_callbacks = sorted(REQUIRED_CALLBACKS - imports)
    forbidden_imports = sorted(FORBIDDEN_IMPORTS & imports)
    query_imports = sorted(
        name for name in imports if name.startswith("emscripten_gl") and "quer" in name.lower()
    )
    if missing_exports:
        raise RuntimeError(f"missing GLX exports: {', '.join(missing_exports)}")
    if missing_callbacks:
        raise RuntimeError(f"missing GLX callbacks: {', '.join(missing_callbacks)}")
    if forbidden_imports or query_imports:
        names = sorted(set(forbidden_imports + query_imports))
        raise RuntimeError(f"forbidden GLX imports remain: {', '.join(names)}")
    if wasm["sha256"] != sha256(raw_wasm):
        raise RuntimeError("godot.wasm.br does not decompress to the current godot.wasm")

    return wasm


def build_info(
    godot_commit: str,
    adapter_commit: str,
    revision: int,
    emcc_version: str,
    brotli_version: str,
    engine_js: Path,
    compressed_wasm: Path,
    loader: Path,
    sdk: Path,
    raw_wasm_sha256: str,
    profile_sha256: str,
    profile_settings: dict[str, bool],
) -> str:
    return f"""# Build Information

- Godot: `4.5.2-stable` (`{GODOT_BASE}`)
- Godot Fork commit: `{godot_commit}`
- Adapter commit: `{adapter_commit}`
- Variant: `glx-2d`
- Artifact revision: `r{revision}`
- GLX alignment source: `citizenll/godot@{GLX_ALIGNMENT}` backported to Godot 4.5.2
- Emscripten: `4.0.10` (`{emcc_version}`)
- WeChat EmscriptenGLX: `0.1.11`
- Build command: `{BUILD_COMMAND}`
- Build profile: `adapter/configs/wechat_2d.py`
- Build profile SHA-256 (LF normalized): `{profile_sha256.upper()}`
- Effective profile settings: `{len(profile_settings)}`
- Post-process: `godot_process.js`
- Compression: `{brotli_version}`
- Device validation: predecessor candidate passed Android real-device rendering; this rebuilt archive requires a same-package smoke test

## Artifact SHA256

- `engine/godot.js`: `{sha256(engine_js).upper()}`
- `engine/godot.wasm.br`: `{sha256(compressed_wasm).upper()}` (`{compressed_wasm.stat().st_size}` bytes)
- decompressed `engine/godot.wasm`: `{raw_wasm_sha256.upper()}` (`{(BUILD_DIR / 'godot.wasm').stat().st_size}` bytes)
- `godot-loader.js`: `{sha256(loader).upper()}`
- `engine/godot-sdk.js`: `{sha256(sdk).upper()}`
"""


def write_deterministic_archive(source_dir: Path, archive_path: Path) -> None:
    files = sorted(path for path in source_dir.rglob("*") if path.is_file())
    with zipfile.ZipFile(
        archive_path,
        "w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for path in files:
            relative = path.relative_to(source_dir).as_posix()
            info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, path.read_bytes(), compresslevel=9)


def verify_archive(source_dir: Path, archive_path: Path) -> None:
    expected = {
        path.relative_to(source_dir).as_posix(): path.read_bytes()
        for path in source_dir.rglob("*")
        if path.is_file()
    }
    missing_required = sorted(REQUIRED_ARCHIVE_ENTRIES - set(expected))
    if missing_required:
        raise RuntimeError(f"template is missing required files: {', '.join(missing_required)}")

    with zipfile.ZipFile(archive_path, "r") as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise RuntimeError("TPZ contains duplicate entries")
        if set(names) != set(expected):
            missing = sorted(set(expected) - set(names))
            extra = sorted(set(names) - set(expected))
            raise RuntimeError(f"TPZ layout mismatch; missing={missing}, extra={extra}")
        if any(name.startswith(f"{TEMPLATE_DIR.name}/") for name in names):
            raise RuntimeError("TPZ must contain template contents, not a top-level template directory")
        for name in names:
            path = Path(name)
            if path.is_absolute() or ".." in path.parts or "\\" in name:
                raise RuntimeError(f"unsafe or non-portable TPZ entry: {name}")
            if archive.read(name) != expected[name]:
                raise RuntimeError(f"TPZ entry differs from template: {name}")
        bad_entry = archive.testzip()
        if bad_entry:
            raise RuntimeError(f"TPZ CRC failure: {bad_entry}")


def atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, pending_name = tempfile.mkstemp(
        prefix=f".{destination.name}.",
        suffix=".tmp",
        dir=destination.parent,
    )
    os.close(handle)
    pending = Path(pending_name)
    try:
        shutil.copyfile(source, pending)
        os.replace(pending, destination)
    finally:
        pending.unlink(missing_ok=True)


def atomic_write(content: bytes, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, pending_name = tempfile.mkstemp(
        prefix=f".{destination.name}.",
        suffix=".tmp",
        dir=destination.parent,
    )
    os.close(handle)
    pending = Path(pending_name)
    try:
        pending.write_bytes(content)
        os.replace(pending, destination)
    finally:
        pending.unlink(missing_ok=True)


def package_template(
    godot_commit: str,
    adapter_commit: str,
    revision: int,
    emcc_version: str,
    brotli_version: str,
    wasm: dict[str, object],
    profile_settings: dict[str, bool],
    profile_sha256: str,
) -> Path:
    artifact_stem = f"{ARTIFACT_PREFIX}-r{revision}"
    archive_path = DIST_DIR / f"{artifact_stem}.tpz"
    build_info_path = DIST_DIR / f"{artifact_stem}.build-info.md"
    profile_info_path = DIST_DIR / f"{artifact_stem}.profile.json"
    checksums_path = DIST_DIR / f"{artifact_stem}.sha256.txt"

    build_js = BUILD_DIR / "godot.js"
    build_wasm_br = BUILD_DIR / "godot.wasm.br"
    runtime_loader = RUNTIME_DIR / "godot-loader.js"
    runtime_sdk = RUNTIME_DIR / "godot-sdk.js"
    for path in (build_js, build_wasm_br, runtime_loader, runtime_sdk):
        require_file(path)

    effective_profile = {
        "schema": 1,
        "artifact": {
            "variant": "glx-2d",
            "revision": revision,
            "filename": archive_path.name,
        },
        "source": "adapter/configs/wechat_2d.py",
        "sha256_lf_normalized": profile_sha256,
        "adapter_commit": adapter_commit,
        "godot_commit": godot_commit,
        "build_arguments": [*BUILD_BASE_ARGUMENTS, "profile=adapter/configs/wechat_2d.py"],
        "verified_against": "godot/.scons_env.json",
        "settings": profile_settings,
    }
    effective_profile_bytes = (
        json.dumps(effective_profile, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")

    with tempfile.TemporaryDirectory(prefix=".glx-package-", dir=REPO_ROOT) as temp:
        temp_dir = Path(temp)
        stage = temp_dir / TEMPLATE_DIR.name
        shutil.copytree(TEMPLATE_DIR, stage)

        stage_js = stage / "engine" / "godot.js"
        stage_wasm_br = stage / "engine" / "godot.wasm.br"
        stage_loader = stage / "godot-loader.js"
        stage_sdk = stage / "engine" / "godot-sdk.js"
        shutil.copyfile(build_js, stage_js)
        shutil.copyfile(build_wasm_br, stage_wasm_br)
        stage_loader.write_bytes(normalized_lf(runtime_loader))
        stage_sdk.write_bytes(normalized_lf(runtime_sdk))

        info = build_info(
            godot_commit,
            adapter_commit,
            revision,
            emcc_version,
            brotli_version,
            stage_js,
            stage_wasm_br,
            stage_loader,
            stage_sdk,
            str(wasm["sha256"]),
            profile_sha256,
            profile_settings,
        )
        info_bytes = info.encode("utf-8")
        (stage / "BUILD_INFO.md").write_bytes(info_bytes)

        staged_archive = temp_dir / archive_path.name
        write_deterministic_archive(stage, staged_archive)
        verify_archive(stage, staged_archive)

        if DIST_DIR.exists():
            shutil.rmtree(DIST_DIR)
        DIST_DIR.mkdir(parents=True)
        os.replace(stage, DIST_TEMPLATE_DIR)
        atomic_copy(staged_archive, archive_path)
        atomic_write(info_bytes, build_info_path)
        atomic_write(effective_profile_bytes, profile_info_path)

    verify_archive(DIST_TEMPLATE_DIR, archive_path)

    checksum_paths = [
        archive_path,
        build_info_path,
        profile_info_path,
        DIST_TEMPLATE_DIR / "engine" / "godot.js",
        DIST_TEMPLATE_DIR / "engine" / "godot.wasm.br",
        DIST_TEMPLATE_DIR / "godot-loader.js",
        DIST_TEMPLATE_DIR / "engine" / "godot-sdk.js",
    ]
    checksums = "".join(
        f"{sha256(path)}  {path.relative_to(DIST_DIR).as_posix()}\n"
        for path in checksum_paths
    )
    atomic_write(checksums.encode("ascii"), checksums_path)
    return archive_path


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("revision must be a positive integer")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Clean-build, validate, and package the Godot 4.5.2 WeChat GLX template."
    )
    parser.add_argument(
        "--scons",
        default=os.environ.get("SCONS", "scons"),
        help="SCons executable (default: SCONS environment variable or scons)",
    )
    parser.add_argument(
        "--revision",
        type=positive_int,
        default=1,
        help="Immutable artifact revision (default: 1).",
    )
    parser.add_argument(
        "--incremental",
        action="store_true",
        help="Skip the SCons clean step. Release packaging should use the default clean build.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    godot_commit = verify_source()
    adapter_commit = run(["git", "rev-parse", "HEAD"])
    emcc_version, brotli_version = verify_tool_versions()
    profile_settings = read_profile_settings(PROFILE_PATH)
    profile_sha256 = sha256_bytes(normalized_lf(PROFILE_PATH))

    build_engine(args.scons, args.incremental, PROFILE_PATH)
    verify_build_environment(profile_settings)

    build_js = BUILD_DIR / "godot.js"
    build_wasm = BUILD_DIR / "godot.wasm"
    build_wasm_br = BUILD_DIR / "godot.wasm.br"
    for path in (build_js, build_wasm, build_wasm_br):
        require_file(path)
    wasm = verify_engine(build_js, build_wasm_br, build_wasm)

    archive_path = package_template(
        godot_commit,
        adapter_commit,
        args.revision,
        emcc_version,
        brotli_version,
        wasm,
        profile_settings,
        profile_sha256,
    )
    print(f"Created {archive_path}")
    print(f"TPZ SHA256: {sha256(archive_path).upper()}")
    print(f"WASM: {build_wasm.stat().st_size} bytes")
    print(f"WASM Brotli: {build_wasm_br.stat().st_size} bytes")
    print(f"Profile settings verified: {len(profile_settings)}")
    print(f"Files: {sum(1 for path in DIST_TEMPLATE_DIR.rglob('*') if path.is_file())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
