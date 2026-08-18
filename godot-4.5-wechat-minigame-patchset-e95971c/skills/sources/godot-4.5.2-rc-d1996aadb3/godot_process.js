"use strict";

const fs = require("fs");

const GODOT_JS_PATH = "./bin/.web_zip/godot.js";

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

let content = fs.readFileSync(GODOT_JS_PATH, "utf8");
for (const rule of RULES) {
    content = applyRule(content, rule);
}

const setValueRule = RULES.find((rule) => rule.name === "setvalue-i64");
if (content.includes(setValueRule.replace) && !content.includes("var tempDouble;var tempI64;")) {
    const marker = "var noExitRuntime=Module[\"noExitRuntime\"]||false;";
    if (!content.includes(marker)) {
        throw new Error("setvalue-i64 was patched but the temp-variable insertion point was not found");
    }
    content = content.replace(marker, `var tempDouble;var tempI64;${marker}`);
    console.log("apply temp-i64-vars");
}

fs.writeFileSync(GODOT_JS_PATH, content);
console.log("Godot WeChat post-process complete");
