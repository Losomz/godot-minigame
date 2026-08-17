const assert = require("assert");
const fs = require("fs");
const vm = require("vm");

class FakeInnerAudioContext {
	constructor() {
		this.listeners = new Map();
		this.src = "";
		this.autoplay = false;
		this.loop = false;
		this.volume = 1;
		this.playbackRate = 1;
		this.startTime = 0;
		this.currentTime = 0;
	}

	on(name, callback) {
		this.listeners.set(name, callback);
	}

	off(name) {
		this.listeners.delete(name);
	}

	emit(name) {
		const callback = this.listeners.get(name);
		if (callback) {
			callback();
		}
	}

	onCanplay(callback) { this.on("canplay", callback); }
	onPlay(callback) { this.on("play", callback); }
	onPause(callback) { this.on("pause", callback); }
	onStop(callback) { this.on("stop", callback); }
	onEnded(callback) { this.on("ended", callback); }
	onTimeUpdate(callback) { this.on("timeupdate", callback); }
	onError(callback) { this.on("error", callback); }
	onWaiting(callback) { this.on("waiting", callback); }
	onSeeking(callback) { this.on("seeking", callback); }
	onSeeked(callback) { this.on("seeked", callback); }
	offCanplay() { this.off("canplay"); }
	offPlay() { this.off("play"); }
	offPause() { this.off("pause"); }
	offStop() { this.off("stop"); }
	offEnded() { this.off("ended"); }
	offTimeUpdate() { this.off("timeupdate"); }
	offError() { this.off("error"); }
	offWaiting() { this.off("waiting"); }
	offSeeking() { this.off("seeking"); }
	offSeeked() { this.off("seeked"); }
	play() {}
	pause() {}
	stop() {}
	destroy() {}
}

const heap = new Float32Array([1, 1]);
const context = {
	ArrayBuffer,
	DataView,
	GameGlobal: {},
	GodotRuntime: {
		allocString: (value) => value,
		free() {},
		heapSub: () => heap,
	},
	HEAPF32: heap,
	HEAPU8: new Uint8Array(0),
	LibraryManager: { library: {} },
	Map,
	Math,
	Number,
	Promise,
	Set,
	Uint8Array,
	console,
	autoAddDeps() {},
	mergeInto() {},
	wx: {
		createInnerAudioContext: () => new FakeInnerAudioContext(),
	},
};

vm.createContext(context);
const source =
	fs.readFileSync("platform/web/js/libs/library_godot_audio.js", "utf8") +
	"\n;globalThis.GodotAudio = _GodotAudio.$GodotAudio; globalThis.__GodotAudio = GodotAudio;";
vm.runInContext(source, context);

const godotAudio = context.__GodotAudio;
const audio = godotAudio.WX;
audio.streamPaths = new Map([
	["stream", { path: "audio/sfx.wav", loopMode: "disabled" }],
]);
audio.activePlaybacks = new Map();
audio.contextPool = [];
audio.busVolumes = new Map();
audio.busMutes = new Map();

let finishedCount = 0;
godotAudio.sampleFinishedCallback = (playbackObjectId) => {
	finishedCount += 1;
	audio.stopSample(playbackObjectId);
};

audio.startSample("playback", "stream", 0, 0, 1, 0);
const playbackContext = audio.activePlaybacks.get("playback").ctx;
playbackContext.emit("ended");

assert.strictEqual(finishedCount, 1);
assert.strictEqual(audio.activePlaybacks.has("playback"), false);
assert.strictEqual(audio.contextPool.length, 1, "context must be returned exactly once");
assert.strictEqual(audio.contextPool[0], playbackContext);
assert.strictEqual(new Set(audio.contextPool).size, audio.contextPool.length);

console.log("WeChat InnerAudioContext single-release test passed");
