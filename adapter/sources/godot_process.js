"use strict";

const fs = require("fs");

const DEFAULT_GODOT_JS_PATH = "./bin/.web_zip/godot.js";
const WASM_IMPORTS_MARKER = "var wasmImports={";

const RULES = [
    {
        name: "disable-window-title",
        required: true,
        match: "function _godot_js_display_window_title_set(p_data){document.title=GodotRuntime.parseString(p_data)}",
        replace: "function _godot_js_display_window_title_set(p_data){}",
    },
    {
        name: "performance-now-polyfill",
        required: true,
        match: "_emscripten_get_now=()=>performance.now()",
        replace: "_emscripten_get_now=nowPolyfill",
    },
    {
        name: "display-pixel-ratio",
        match: "getPixelRatio:function(){return GodotDisplayScreen.hidpi?window.devicePixelRatio||1:1}",
        replace: "getPixelRatio:function(){if(!GodotDisplayScreen.hidpi){return 1}let ratio=Number((typeof GameGlobal!==\"undefined\"&&GameGlobal.__godotMinigamePixelRatio)||window.devicePixelRatio)||1;try{if(typeof wx!==\"undefined\"){const info=wx.getWindowInfo?wx.getWindowInfo():wx.getSystemInfoSync?wx.getSystemInfoSync():null;if(info){ratio=Number(info.pixelRatio||info.devicePixelRatio||ratio)||ratio}}}catch(e){}return Math.max(1,ratio)}",
    },
    {
        name: "getvalue-i64",
        match: "case\"i64\":abort(\"to do getValue(i64) use WASM_BIGINT\");",
        replace: "case\"i64\":return HEAP32[ptr>>2];",
    },
    {
        name: "setvalue-i64",
        match: "case\"i64\":abort(\"to do setValue(i64) use WASM_BIGINT\");",
        replace: "case\"i64\":tempI64=[value>>>0,(tempDouble=value,+Math.abs(tempDouble)>=1?tempDouble>0?+Math.floor(tempDouble/4294967296)>>>0:~~+Math.ceil((tempDouble- +(~~tempDouble>>>0))/4294967296)>>>0:0)],HEAP32[ptr>>2]=tempI64[0],HEAP32[ptr+4>>2]=tempI64[1];break;",
    },
    {
        name: "wx-fs-mount",
        required: true,
        match: "FS.mount(IDBFS,{},path)",
        replace: "FS.mount(WXMEMFS,{},path)",
    },
    {
        name: "wasm-runtimeerror-instanceof",
        match: "e instanceof WebAssembly.RuntimeError",
        replace: "e",
        detectApplied: false,
    },
    {
        name: "wasm-runtimeerror-constructor",
        match: "var e=new WebAssembly.RuntimeError(what);",
        replace: "var e=(typeof WebAssembly!==\"undefined\"&&typeof WebAssembly.RuntimeError===\"function\")?new WebAssembly.RuntimeError(what):new Error(what);",
    },
    {
        name: "wx-glx-after-do-frame-wrapper",
        match: "Module.wxContextGlobal.registerAfterDoFrame(Module._glxCommandBufferFlush)",
        replace: "Module.wxContextGlobal.registerAfterDoFrame(function(){return Module._glxCommandBufferFlush();})",
    },
    {
        name: "stat-ino-guard",
        match: "HEAP64[buf+88>>3]=BigInt(stat.ino);",
        replace: "if(stat.ino)HEAP64[buf+88>>3]=BigInt(stat.ino);",
    },
];

function applyRule(content, rule) {
    const canDetectApplied = rule.detectApplied !== false;
    if (canDetectApplied && content.includes(rule.replace)) {
        console.log(`already applied ${rule.name}`);
        return content;
    }
    if (!content.includes(rule.match)) {
        if (rule.required) {
            throw new Error(`required pattern not found: ${rule.name}`);
        }
        console.log(`skip ${rule.name}: pattern not present in this Emscripten output`);
        return content;
    }
    console.log(`apply ${rule.name}`);
    return content.replace(rule.match, rule.replace);
}

function ensureInvokeImportMappings(content) {
    const invokeMatches = Array.from(content.matchAll(/function\s+(invoke_[A-Za-z0-9_]+)\s*\(/g));
    const invokeNames = Array.from(new Set(invokeMatches.map((match) => match[1]))).sort();
    const missingMappings = invokeNames
        .filter((name) => !content.includes(`${name}:${name}`))
        .map((name) => `${name}:${name}`);

    if (missingMappings.length === 0) {
        return content;
    }

    console.log(`apply invoke-imports: ${missingMappings.length} missing mappings`);
    return content.replace(
        WASM_IMPORTS_MARKER,
        `${WASM_IMPORTS_MARKER}${missingMappings.join(",")},`
    );
}

function ensureCommitFrameShim(content) {
    const symbol = "_emscripten_webgl_commit_frame";
    const mapping = `emscripten_webgl_commit_frame:${symbol}`;
    if (!content.includes(symbol) || content.includes(mapping)) {
        return content;
    }
    if (!content.includes(WASM_IMPORTS_MARKER)) {
        throw new Error("commit-frame is referenced but wasmImports was not found");
    }

    console.log("apply commit-frame-shim");
    return content.replace(
        WASM_IMPORTS_MARKER,
        `var ${symbol}=function(){};${WASM_IMPORTS_MARKER}${mapping},`
    );
}

function processGodotJS(content) {
    content = ensureInvokeImportMappings(content);
    content = ensureCommitFrameShim(content);

    for (const rule of RULES) {
        content = applyRule(content, rule);
    }

    const setValueRule = RULES.find((rule) => rule.name === "setvalue-i64");
    if (
        content.includes(setValueRule.replace) &&
        !content.includes("var tempDouble;var tempI64;")
    ) {
        const marker = "var noExitRuntime=Module[\"noExitRuntime\"]||false;";
        if (!content.includes(marker)) {
            throw new Error(
                "setvalue-i64 was patched but the temp-variable insertion point was not found"
            );
        }
        content = content.replace(marker, `var tempDouble;var tempI64;${marker}`);
        console.log("apply temp-i64-vars");
    }

    return content;
}

function main() {
    const godotJSPath = process.argv[2] || DEFAULT_GODOT_JS_PATH;
    const content = fs.readFileSync(godotJSPath, "utf8");
    fs.writeFileSync(godotJSPath, processGodotJS(content));
    console.log(`Godot WeChat post-process complete: ${godotJSPath}`);
}

if (require.main === module) {
    main();
}

module.exports = {
    ensureCommitFrameShim,
    processGodotJS,
};
