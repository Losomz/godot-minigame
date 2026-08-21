"use strict";

const fs = require("fs");

const DEFAULT_GODOT_JS_PATH = "./bin/.web_zip/godot.js";

// Emscripten generated WASM imports object marker. Missing GLX imports are inserted here.
const WASM_IMPORTS_MARKER = "var wasmImports={";
const WX_GLX_BRIDGE_INSERTION_MARKER = "addOnPostRun(function(){GL.getSource=";
const WX_GLX_BRIDGE_MARKER = "function wxGLXGetNativeExport";
const WX_GLX_NATIVE_EXPORT_NAMES = [
    "glxInit",
    "glxInitBufferDataAndGlState",
    "glxUpdateContextId",
    "glxCommandBufferFlush",
];

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
        name: "wx-glx-after-do-frame-wrapper",
        // Wrap the WASM export in a plain JavaScript callback for the WeChat frame hook.
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

// Serialized into Emscripten's module factory, where Module, GL, wx, and GameGlobal exist.
function installWXGLXRuntimeBridge() {
    function wxGLXGetNativeExport(name) {
        if (typeof Module === "undefined" || !Module) {
            return null;
        }
        const exportName = "_" + name;
        try {
            const nativeFunction = Module[exportName];
            return typeof nativeFunction === "function" ? nativeFunction : null;
        } catch (err) {
            return null;
        }
    }

    function wxGLXHasNativeBindings() {
        return (
            !!wxGLXGetNativeExport("glxInit") &&
            !!wxGLXGetNativeExport("glxInitBufferDataAndGlState") &&
            !!wxGLXGetNativeExport("glxUpdateContextId")
        );
    }

    function wxGLXGetRoot() {
        if (typeof GameGlobal !== "undefined") {
            return GameGlobal;
        }
        if (typeof globalThis !== "undefined") {
            return globalThis;
        }
        return {};
    }

    function wxGLXGetPinnedMode() {
        const mode = wxGLXGetRoot().__godotMinigameWXGLXEnabled;
        return typeof mode === "boolean" ? mode : null;
    }

    function wxGLXIsRuntimeSupported() {
        const pinnedMode = wxGLXGetPinnedMode();
        if (pinnedMode !== null) {
            if (pinnedMode && !wxGLXHasNativeBindings()) {
                throw new Error(
                    "[WXGLX] The loader selected WXGLX, but the native bindings are missing."
                );
            }
            return pinnedMode;
        }
        return (
            wxGLXGetRoot().__GODOT_DISABLE_WXGLX !== true &&
            typeof wx !== "undefined" &&
            !!wx.env &&
            !!wx.env.isSupportEmscriptenGLX &&
            wxGLXHasNativeBindings()
        );
    }

    function wxGLXValidateAndPinContext(glContext) {
        if (!glContext) {
            return;
        }
        const root = wxGLXGetRoot();
        const actualMode = !!glContext.emscriptenGLX;
        const pinnedMode = wxGLXGetPinnedMode();
        if (pinnedMode !== null && pinnedMode !== actualMode) {
            throw new Error(
                `[WXGLX] Canvas context mode mismatch: loader=${
                    pinnedMode ? "wxwebgl" : "webgl"
                }, engine=${actualMode ? "wxwebgl" : "webgl"}.`
            );
        }
        root.__godotMinigameWXGLXEnabled = actualMode;
    }

    function wxGLXCallNative(name, args) {
        const nativeFunction = wxGLXGetNativeExport(name);
        if (!nativeFunction) {
            return null;
        }
        try {
            return nativeFunction.apply(Module, args || []);
        } catch (err) {
            console.warn("[WXGLX] native call failed:", name, err);
            return null;
        }
    }

    function wxGLXInitContext(glContext) {
        if (!glContext) {
            return;
        }
        const glxContext = glContext.emscriptenGLX;
        wxGLXValidateAndPinContext(glContext);
        wxGLXCallNative("glxInit", [!!glxContext]);
        if (!glxContext) {
            return;
        }
        if (typeof Module.wxContextGlobal === "undefined") {
            Module.wxContextGlobal = Object.assign({}, glxContext);
            wxGLXCallNative("glxInitBufferDataAndGlState", [
                glxContext.isWebGL2 ? 2 : 1,
                glxContext.platform,
            ]);
        }
        wxGLXCallNative("glxUpdateContextId", [glxContext.ctxid]);
    }

    function wxGLXPatchCreateContext() {
        if (typeof GL === "undefined" || typeof GL.createContext !== "function") {
            return;
        }
        const originalCreateContext = GL.createContext;
        GL.createContext = function (canvas, webGLContextAttributes) {
            let createdContext = null;
            let originalGetContext = null;
            let contextInitializedInGetContext = false;
            if (canvas && typeof canvas.getContext === "function") {
                originalGetContext = canvas.getContext;
                canvas.getContext = function (contextType, contextAttributes) {
                    let requestedType = contextType;
                    if (wxGLXIsRuntimeSupported()) {
                        if (contextType === "webgl2") {
                            requestedType = "wxwebgl2";
                        } else if (contextType === "webgl") {
                            requestedType = "wxwebgl";
                        }
                    }
                    createdContext = originalGetContext.call(
                        this,
                        requestedType,
                        contextAttributes
                    );
                    if (!createdContext && requestedType !== contextType) {
                        throw new Error(
                            `[WXGLX] Failed to create pinned ${requestedType} context.`
                        );
                    }
                    if (createdContext) {
                        wxGLXInitContext(createdContext);
                        contextInitializedInGetContext = true;
                    }
                    return createdContext;
                };
            }
            try {
                const handle = originalCreateContext.apply(this, arguments);
                if (!contextInitializedInGetContext) {
                    const contextRecord = GL.contexts && GL.contexts[handle];
                    const glContext =
                        contextRecord && contextRecord.GLctx
                            ? contextRecord.GLctx
                            : createdContext;
                    wxGLXInitContext(glContext);
                }
                return handle;
            } finally {
                if (canvas && originalGetContext) {
                    canvas.getContext = originalGetContext;
                }
            }
        };
    }

    function wxGLXPatchMakeContextCurrent() {
        if (typeof GL === "undefined" || typeof GL.makeContextCurrent !== "function") {
            return;
        }
        const originalMakeContextCurrent = GL.makeContextCurrent;
        GL.makeContextCurrent = function (contextHandle) {
            const result = originalMakeContextCurrent.apply(this, arguments);
            const currentContext =
                GL.currentContext && GL.currentContext.GLctx
                    ? GL.currentContext.GLctx
                    : null;
            if (currentContext && currentContext.emscriptenGLX) {
                wxGLXCallNative("glxUpdateContextId", [
                    currentContext.emscriptenGLX.ctxid,
                ]);
            }
            return result;
        };
    }

    function installWXGLXPatches() {
        if (typeof GL === "undefined" || GL.__godotWXGLXPatched) {
            return;
        }
        GL.__godotWXGLXPatched = true;
        wxGLXPatchCreateContext();
        wxGLXPatchMakeContextCurrent();
    }

    installWXGLXPatches();
}

function buildWXGLXRuntimeBridge() {
    return `(${installWXGLXRuntimeBridge.toString()})();`;
}

function hasNativeExport(content, name) {
    return (
        content.includes(`Module["_${name}"]`) ||
        content.includes(`createExportWrapper("${name}"`)
    );
}

function ensureWXGLXRuntimeBridge(content) {
    const bridgeCount = content.split(WX_GLX_BRIDGE_MARKER).length - 1;
    const foundExports = WX_GLX_NATIVE_EXPORT_NAMES.filter((name) =>
        hasNativeExport(content, name)
    );
    if (
        foundExports.length > 0 &&
        foundExports.length !== WX_GLX_NATIVE_EXPORT_NAMES.length
    ) {
        const missingExports = WX_GLX_NATIVE_EXPORT_NAMES.filter(
            (name) => !foundExports.includes(name)
        );
        throw new Error(
            `incomplete WXGLX native exports; missing: ${missingExports.join(", ")}`
        );
    }

    if (bridgeCount > 0) {
        if (foundExports.length !== WX_GLX_NATIVE_EXPORT_NAMES.length) {
            throw new Error(
                "existing WXGLX runtime bridge has no complete native export set"
            );
        }
        if (bridgeCount !== 1) {
            throw new Error(`WXGLX runtime bridge count must be 1, found ${bridgeCount}`);
        }

        const requiredBridgePatterns = [
            ["create-context patch", /function\s+wxGLXPatchCreateContext\s*\(/g, 1],
            ["make-current patch", /function\s+wxGLXPatchMakeContextCurrent\s*\(/g, 1],
            ["patch installer", /function\s+installWXGLXPatches\s*\(/g, 1],
            ["wxwebgl2 mapping", /requestedType\s*=\s*"wxwebgl2"/g, 1],
            ["glxInit call", /wxGLXCallNative\(\s*"glxInit"\s*,/g, 1],
            [
                "glxInitBufferDataAndGlState call",
                /wxGLXCallNative\(\s*"glxInitBufferDataAndGlState"\s*,/g,
                1,
            ],
            [
                "glxUpdateContextId calls",
                /wxGLXCallNative\(\s*"glxUpdateContextId"\s*,/g,
                2,
            ],
            ["patch installer invocation", /installWXGLXPatches\(\);/g, 1],
        ];
        for (const [name, pattern, expectedCount] of requiredBridgePatterns) {
            const actualCount = Array.from(content.matchAll(pattern)).length;
            if (actualCount !== expectedCount) {
                throw new Error(
                    `existing WXGLX runtime bridge is incomplete: ${name} count ` +
                        `must be ${expectedCount}, found ${actualCount}`
                );
            }
        }

        const insertionCount =
            content.split(WX_GLX_BRIDGE_INSERTION_MARKER).length - 1;
        const runIndex = content.indexOf("preInit();run();");
        const bridgeIndex = content.indexOf(WX_GLX_BRIDGE_MARKER);
        const insertionIndex = content.indexOf(WX_GLX_BRIDGE_INSERTION_MARKER);
        if (
            insertionCount !== 1 ||
            runIndex < 0 ||
            !(runIndex < bridgeIndex && bridgeIndex < insertionIndex)
        ) {
            throw new Error(
                "existing WXGLX runtime bridge is not between run() and the GL post-run hook"
            );
        }

        console.log("already applied wx-glx-runtime-bridge");
        return content;
    }

    if (foundExports.length === 0) {
        console.log("skip wx-glx-runtime-bridge: native exports not present");
        return content;
    }

    const insertionCount = content.split(WX_GLX_BRIDGE_INSERTION_MARKER).length - 1;
    if (insertionCount !== 1) {
        throw new Error(
            `WXGLX bridge insertion point count must be 1, found ${insertionCount}`
        );
    }

    console.log("apply wx-glx-runtime-bridge");
    return content.replace(
        WX_GLX_BRIDGE_INSERTION_MARKER,
        `${buildWXGLXRuntimeBridge()}${WX_GLX_BRIDGE_INSERTION_MARKER}`
    );
}

function ensureInvokeImportMappings(content) {
    // GLX can introduce Emscripten invoke_* wrappers that are missing from wasmImports.
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
    // GLX performs the real frame flush; this shim only satisfies the WASM import.
    const mapping = "emscripten_webgl_commit_frame:_emscripten_webgl_commit_frame";
    if (content.includes(mapping)) {
        return content;
    }

    console.log("apply commit-frame-shim");
    return content.replace(
        WASM_IMPORTS_MARKER,
        `var _emscripten_webgl_commit_frame=function(){};${WASM_IMPORTS_MARKER}${mapping},`
    );
}

function processGodotJS(content) {
    content = ensureInvokeImportMappings(content);
    content = ensureCommitFrameShim(content);
    content = ensureWXGLXRuntimeBridge(content);

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
    buildWXGLXRuntimeBridge,
    ensureWXGLXRuntimeBridge,
    processGodotJS,
};
