"use strict";

const fs = require("fs");
const path = require("path");

const REQUIRED_LOADER_ANCHORS = [
    "getWindowInfo()",
    "__godotMinigamePixelRatio",
    "syncWindowDevicePixelRatio",
    "drawCoverImage",
    "normalizeProgress",
    "this.gl.commit",
];

function validateTemplateRuntime(templateDir, mode = "webgl2") {
    if (mode !== "webgl2" && mode !== "wxglx") {
        throw new Error(`Unsupported runtime mode: ${mode}`);
    }

    const root = path.resolve(templateDir);
    const loaderPath = path.join(root, "godot-loader.js");
    const loader = fs.readFileSync(loaderPath, "utf8");

    for (const anchor of REQUIRED_LOADER_ANCHORS) {
        if (!loader.includes(anchor)) {
            throw new Error(`${loaderPath} is stale: missing ${anchor}`);
        }
    }
    if (loader.includes("this.dpr=window.devicePixelRatio||1")) {
        throw new Error(`${loaderPath} still uses the legacy DPR path`);
    }

    const wxglxAnchors = [
        "__GODOT_DISABLE_WXGLX",
        "__godotMinigameWXGLXEnabled",
        '"wxwebgl2"',
    ];
    if (mode === "wxglx") {
        for (const anchor of wxglxAnchors) {
            if (!loader.includes(anchor)) {
                throw new Error(`${loaderPath} is missing WXGLX selection: ${anchor}`);
            }
        }
    } else if (loader.includes('"wxwebgl2"')) {
        throw new Error(`${loaderPath} must not enable WXGLX`);
    }

    const enginePath = path.join(root, "engine", "godot.js");
    const engine = fs.readFileSync(enginePath, "utf8");
    if (/var\s+_emscripten_webgl_commit_frame\s*=\s*function\s*\(\s*\)\s*\{\s*\}\s*;/.test(engine)) {
        throw new Error(`${enginePath} replaces Emscripten's frame commit with a no-op`);
    }
    if (
        engine.includes("_emscripten_webgl_do_commit_frame") &&
        !engine.includes("__godotMinigameOriginalCommitFrame")
    ) {
        throw new Error(`${enginePath} does not wrap Emscripten's frame commit for WeChat presentation`);
    }
    const ratioStart = engine.indexOf("getPixelRatio:function()");
    const ratioBlock = ratioStart >= 0 ? engine.slice(ratioStart, ratioStart + 700) : "";
    if (!ratioBlock.includes("getWindowInfo")) {
        throw new Error(`${enginePath} does not resolve DPR from WeChat window info`);
    }

    return { loaderPath, enginePath, mode };
}

if (require.main === module) {
    const [, , templateDir, mode = "webgl2"] = process.argv;
    if (!templateDir) {
        console.error("Usage: node validate_template_runtime.js <template-dir> [webgl2|wxglx]");
        process.exit(2);
    }

    try {
        const result = validateTemplateRuntime(templateDir, mode);
        console.log(`Validated ${result.mode} template runtime: ${path.resolve(templateDir)}`);
    } catch (error) {
        console.error(error.message);
        process.exit(1);
    }
}

module.exports = { validateTemplateRuntime };
