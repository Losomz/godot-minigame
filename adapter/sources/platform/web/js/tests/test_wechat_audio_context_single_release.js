const assert = require("assert");
const fs = require("fs");
const vm = require("vm");

class FakeInnerAudioContext {
	constructor() {
		this.listeners = new Map();
		this._src = "";
		this.srcAssignments = [];
		this.internalErrorListenerCount = 0;
		this.destroyed = false;
		this.autoplay = false;
		this.loop = false;
		this.volume = 1;
		this.playbackRate = 1;
		this.startTime = 0;
		this.currentTime = 0;
	}

	get src() {
		return this._src;
	}

	set src(value) {
		this._src = value;
		this.srcAssignments.push(value);
		this.internalErrorListenerCount += 1;
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
	destroy() { this.destroyed = true; }
}

const heap = new Float32Array([1, 1]);
const createdContexts = [];
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
		createInnerAudioContext: () => {
			const audioContext = new FakeInnerAudioContext();
			createdContexts.push(audioContext);
			return audioContext;
		},
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

godotAudio.sampleFinishedCallback = null;
audio.streamPaths.set("stream-a", { path: "audio/a.wav", loopMode: "disabled" });
audio.streamPaths.set("stream-b", { path: "audio/b.wav", loopMode: "disabled" });

function playAndStop(playbackId, streamId) {
	audio.startSample(playbackId, streamId, 0, 0, 1, 0);
	const audioContext = audio.activePlaybacks.get(playbackId).ctx;
	audio.stopSample(playbackId);
	return audioContext;
}

const contextA = playAndStop("playback-a", "stream-a");
const contextB = playAndStop("playback-b", "stream-b");

assert.notStrictEqual(contextA, contextB, "different sources must not share a context");
assert.deepStrictEqual(contextA.srcAssignments, ["audio/a.wav"]);
assert.deepStrictEqual(contextB.srcAssignments, ["audio/b.wav"]);

for (let index = 0; index < 30; index += 1) {
	const useA = index % 2 === 0;
	const reusedContext = playAndStop(
		`alternating-playback-${index}`,
		useA ? "stream-a" : "stream-b"
	);
	assert.strictEqual(reusedContext, useA ? contextA : contextB);
}

assert.strictEqual(contextA.internalErrorListenerCount, 1);
assert.strictEqual(contextB.internalErrorListenerCount, 1);
assert.deepStrictEqual(contextA.srcAssignments, ["audio/a.wav"]);
assert.deepStrictEqual(contextB.srcAssignments, ["audio/b.wav"]);

const uniqueContexts = [];
for (let index = 0; index < audio.MAX_POOL_SIZE + 3; index += 1) {
	const streamId = `unique-stream-${index}`;
	audio.streamPaths.set(streamId, {
		path: `audio/unique-${index}.wav`,
		loopMode: "disabled",
	});
	uniqueContexts.push(playAndStop(`unique-playback-${index}`, streamId));
}

assert.strictEqual(audio.contextPool.length, audio.MAX_POOL_SIZE);
assert.strictEqual(new Set(audio.contextPool).size, audio.contextPool.length);
assert.strictEqual(playbackContext.destroyed, true, "the least recently used context must be evicted first");
assert.strictEqual(contextA.destroyed, true);
assert.strictEqual(contextB.destroyed, true);
assert.strictEqual(uniqueContexts.at(-1).destroyed, false);
assert.ok(audio.contextPool.includes(uniqueContexts.at(-1)), "the most recently used context must stay pooled");
for (const audioContext of createdContexts) {
	assert.ok(audioContext.srcAssignments.length <= 1, "a context source must be assigned at most once");
	assert.ok(audioContext.internalErrorListenerCount <= 1, "internal error listeners must not accumulate");
}

console.log("WeChat InnerAudioContext pool regression tests passed");
