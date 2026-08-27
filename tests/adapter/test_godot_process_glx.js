"use strict";

const assert = require("assert");

const {
    ensureCommitFrameShim,
    processGodotJS,
} = require("../../adapter/sources/godot_process.js");

const mapping = "emscripten_webgl_commit_frame:_emscripten_webgl_commit_frame";
const shim = "var _emscripten_webgl_commit_frame=function(){};";
const imports = "var wasmImports={other:other};";

assert.strictEqual(ensureCommitFrameShim(imports), imports);

const referencedCommitFrame =
    "function commit(){return _emscripten_webgl_commit_frame()}" + imports;
const shimmedCommitFrame = ensureCommitFrameShim(referencedCommitFrame);
assert.ok(shimmedCommitFrame.includes(shim));
assert.ok(shimmedCommitFrame.includes(mapping));
assert.strictEqual(ensureCommitFrameShim(shimmedCommitFrame), shimmedCommitFrame);
assert.throws(
    () => ensureCommitFrameShim("_emscripten_webgl_commit_frame()"),
    /wasmImports was not found/
);

const emscriptenCommitFrame =
    "var _emscripten_webgl_commit_frame=_emscripten_webgl_do_commit_frame;" +
    `var wasmImports={${mapping},other:other};`;
assert.strictEqual(ensureCommitFrameShim(emscriptenCommitFrame), emscriptenCommitFrame);

const fixture = [
    "function _godot_js_display_window_title_set(p_data){document.title=GodotRuntime.parseString(p_data)};",
    "_emscripten_get_now=()=>performance.now();",
    "FS.mount(IDBFS,{},path);",
    "if(e instanceof WebAssembly.RuntimeError){};",
    "var e=new WebAssembly.RuntimeError(what);",
    "Module.wxContextGlobal.registerAfterDoFrame(Module._glxCommandBufferFlush);",
    "HEAP64[buf+88>>3]=BigInt(stat.ino);",
    "function invoke_vii(index,a,b){};",
    imports,
].join("");
const processedFixture = processGodotJS(fixture);

assert.ok(processedFixture.includes("invoke_vii:invoke_vii"));
assert.ok(processedFixture.includes("typeof WebAssembly.RuntimeError===\"function\""));
assert.ok(
    processedFixture.includes(
        "registerAfterDoFrame(function(){return Module._glxCommandBufferFlush();})"
    )
);
assert.ok(processedFixture.includes("if(stat.ino)HEAP64[buf+88>>3]"));
assert.ok(!processedFixture.includes("instanceof WebAssembly.RuntimeError"));
assert.ok(!processedFixture.includes("function wxGLXGetNativeExport"));
assert.ok(!processedFixture.includes(shim));
assert.ok(!processedFixture.includes(mapping));
assert.strictEqual(processGodotJS(processedFixture), processedFixture);

console.log("godot_process GLX alignment tests passed");
