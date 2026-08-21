"use strict";

const assert = require("assert");
const vm = require("vm");

const {
    buildWXGLXRuntimeBridge,
    ensureWXGLXRuntimeBridge,
} = require("../sources/godot_process.js");

const NATIVE_EXPORT_NAMES = [
    "glxInit",
    "glxInitBufferDataAndGlState",
    "glxUpdateContextId",
    "glxCommandBufferFlush",
];
const INSERTION_MARKER = "addOnPostRun(function(){GL.getSource=";

function countOccurrences(content, needle) {
    return content.split(needle).length - 1;
}

function generatedFixture(exportNames = NATIVE_EXPORT_NAMES, includeInsertionMarker = true) {
    const exports = exportNames
        .map(
            (name) =>
                `let _${name}=Module["_${name}"]=` +
                `makeInvalidEarlyAccess("_${name}");`
        )
        .join("");
    const tail = includeInsertionMarker
        ? `${INSERTION_MARKER}function(){});`
        : "addOnPostRun(function(){});";
    return `${exports}preInit();run();${tail}`;
}

{
    const source = generatedFixture();
    const processed = ensureWXGLXRuntimeBridge(source);
    assert.notStrictEqual(processed, source);
    assert.strictEqual(countOccurrences(processed, "function wxGLXGetNativeExport"), 1);
    assert.ok(
        processed.indexOf("function wxGLXGetNativeExport") <
            processed.indexOf(INSERTION_MARKER)
    );
    assert.strictEqual(ensureWXGLXRuntimeBridge(processed), processed);

    const duplicated = processed.replace(
        INSERTION_MARKER,
        `${buildWXGLXRuntimeBridge()}${INSERTION_MARKER}`
    );
    assert.throws(
        () => ensureWXGLXRuntimeBridge(duplicated),
        /runtime bridge count must be 1/
    );
}

{
    const truncated = generatedFixture().replace(
        INSERTION_MARKER,
        `function wxGLXGetNativeExport(){}${INSERTION_MARKER}`
    );
    assert.throws(
        () => ensureWXGLXRuntimeBridge(truncated),
        /existing WXGLX runtime bridge is incomplete/
    );
}

{
    const misplaced = generatedFixture().replace(
        "preInit();run();",
        `${buildWXGLXRuntimeBridge()}preInit();run();`
    );
    assert.throws(
        () => ensureWXGLXRuntimeBridge(misplaced),
        /not between run\(\) and the GL post-run hook/
    );
}

{
    const source = `preInit();run();${INSERTION_MARKER}function(){});`;
    assert.strictEqual(ensureWXGLXRuntimeBridge(source), source);
}

assert.throws(
    () => ensureWXGLXRuntimeBridge(generatedFixture(["glxInit"])),
    /incomplete WXGLX native exports/
);
assert.throws(
    () => ensureWXGLXRuntimeBridge(generatedFixture(NATIVE_EXPORT_NAMES, false)),
    /insertion point count must be 1/
);

function createRuntimeHarness({
    contexts,
    nativeBindings = true,
    pinnedMode,
    supported = true,
}) {
    const contextQueue = contexts.slice();
    const contextRequests = [];
    const nativeCalls = [];
    const warnings = [];
    let nextHandle = 1;

    const Module = {};
    if (nativeBindings) {
        Module._glxInit = (enabled) => nativeCalls.push(["glxInit", enabled]);
        Module._glxInitBufferDataAndGlState = (version, platform) =>
            nativeCalls.push(["glxInitBufferDataAndGlState", version, platform]);
        Module._glxUpdateContextId = (contextId) =>
            nativeCalls.push(["glxUpdateContextId", contextId]);
    }

    const GameGlobal = {};
    if (typeof pinnedMode === "boolean") {
        GameGlobal.__godotMinigameWXGLXEnabled = pinnedMode;
    }

    const canvas = {
        getContext(type) {
            contextRequests.push(type);
            return contextQueue.length ? contextQueue.shift() : null;
        },
    };
    const baseGetContext = canvas.getContext;

    const GL = {
        contexts: {},
        currentContext: null,
        createContext(target, attributes) {
            const glContext = target.getContext("webgl2", attributes);
            if (!glContext) {
                return 0;
            }
            const handle = nextHandle++;
            this.contexts[handle] = { GLctx: glContext };
            return handle;
        },
        makeContextCurrent(handle) {
            this.currentContext = this.contexts[handle] || null;
            return true;
        },
    };

    const context = {
        GameGlobal,
        GL,
        Module,
        console: {
            log() {},
            warn(...args) {
                warnings.push(args);
            },
        },
        wx: { env: { isSupportEmscriptenGLX: supported } },
    };
    vm.createContext(context);
    vm.runInContext(buildWXGLXRuntimeBridge(), context);

    return {
        baseGetContext,
        canvas,
        context,
        contextRequests,
        GameGlobal,
        GL,
        Module,
        nativeCalls,
        warnings,
    };
}

function createGLXContext(contextId, platform = 7) {
    return {
        emscriptenGLX: {
            ctxid: contextId,
            isWebGL2: true,
            platform,
        },
    };
}

{
    const firstContext = createGLXContext(101);
    const secondContext = createGLXContext(202);
    const harness = createRuntimeHarness({
        contexts: [firstContext, secondContext],
        pinnedMode: true,
    });

    const firstHandle = harness.GL.createContext(harness.canvas, {});
    assert.strictEqual(firstHandle, 1);
    assert.deepStrictEqual(harness.contextRequests, ["wxwebgl2"]);
    assert.deepStrictEqual(harness.nativeCalls, [
        ["glxInit", true],
        ["glxInitBufferDataAndGlState", 2, 7],
        ["glxUpdateContextId", 101],
    ]);
    assert.strictEqual(harness.Module.wxContextGlobal.ctxid, 101);
    assert.strictEqual(harness.Module.wxContextGlobal.isWebGL2, true);
    assert.strictEqual(harness.Module.wxContextGlobal.platform, 7);
    assert.notStrictEqual(harness.Module.wxContextGlobal, firstContext.emscriptenGLX);

    const wxContextGlobal = harness.Module.wxContextGlobal;
    const secondHandle = harness.GL.createContext(harness.canvas, {});
    assert.strictEqual(secondHandle, 2);
    assert.strictEqual(harness.Module.wxContextGlobal, wxContextGlobal);
    assert.deepStrictEqual(harness.contextRequests, ["wxwebgl2", "wxwebgl2"]);
    assert.deepStrictEqual(harness.nativeCalls.slice(3), [
        ["glxInit", true],
        ["glxUpdateContextId", 202],
    ]);

    harness.GL.makeContextCurrent(firstHandle);
    harness.GL.makeContextCurrent(secondHandle);
    assert.deepStrictEqual(harness.nativeCalls.slice(-2), [
        ["glxUpdateContextId", 101],
        ["glxUpdateContextId", 202],
    ]);
    assert.strictEqual(harness.canvas.getContext, harness.baseGetContext);

    const patchedCreateContext = harness.GL.createContext;
    vm.runInContext(buildWXGLXRuntimeBridge(), harness.context);
    assert.strictEqual(harness.GL.createContext, patchedCreateContext);
}

{
    const deferredContext = createGLXContext(250);
    const harness = createRuntimeHarness({
        contexts: [deferredContext],
        nativeBindings: false,
        pinnedMode: true,
    });
    harness.Module._glxInit = (enabled) =>
        harness.nativeCalls.push(["glxInit", enabled]);
    harness.Module._glxInitBufferDataAndGlState = (version, platform) =>
        harness.nativeCalls.push([
            "glxInitBufferDataAndGlState",
            version,
            platform,
        ]);
    harness.Module._glxUpdateContextId = (contextId) =>
        harness.nativeCalls.push(["glxUpdateContextId", contextId]);

    assert.strictEqual(harness.GL.createContext(harness.canvas, {}), 1);
    assert.deepStrictEqual(harness.contextRequests, ["wxwebgl2"]);
    assert.deepStrictEqual(harness.nativeCalls, [
        ["glxInit", true],
        ["glxInitBufferDataAndGlState", 2, 7],
        ["glxUpdateContextId", 250],
    ]);
}

{
    const harness = createRuntimeHarness({
        contexts: [{}],
        pinnedMode: false,
    });
    assert.strictEqual(harness.GL.createContext(harness.canvas, {}), 1);
    assert.deepStrictEqual(harness.contextRequests, ["webgl2"]);
    assert.deepStrictEqual(harness.nativeCalls, [["glxInit", false]]);
    assert.strictEqual(harness.Module.wxContextGlobal, undefined);
}

{
    const harness = createRuntimeHarness({
        contexts: [{}],
        supported: false,
    });
    assert.strictEqual(harness.GL.createContext(harness.canvas, {}), 1);
    assert.deepStrictEqual(harness.contextRequests, ["webgl2"]);
    assert.strictEqual(harness.GameGlobal.__godotMinigameWXGLXEnabled, false);
}

{
    const harness = createRuntimeHarness({
        contexts: [createGLXContext(303)],
        nativeBindings: false,
        pinnedMode: true,
    });
    assert.throws(
        () => harness.GL.createContext(harness.canvas, {}),
        /loader selected WXGLX, but the native bindings are missing/
    );
    assert.deepStrictEqual(harness.contextRequests, []);
    assert.strictEqual(harness.canvas.getContext, harness.baseGetContext);
}

{
    const harness = createRuntimeHarness({
        contexts: [null],
        pinnedMode: true,
    });
    assert.throws(
        () => harness.GL.createContext(harness.canvas, {}),
        /Failed to create pinned wxwebgl2 context/
    );
    assert.deepStrictEqual(harness.contextRequests, ["wxwebgl2"]);
    assert.strictEqual(harness.canvas.getContext, harness.baseGetContext);
}

{
    const harness = createRuntimeHarness({
        contexts: [createGLXContext(404)],
        pinnedMode: false,
    });
    assert.throws(
        () => harness.GL.createContext(harness.canvas, {}),
        /Canvas context mode mismatch/
    );
    assert.deepStrictEqual(harness.contextRequests, ["webgl2"]);
    assert.strictEqual(harness.canvas.getContext, harness.baseGetContext);
}

console.log("Godot post-process WXGLX bridge tests passed");
