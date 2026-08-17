const assert = require("assert");
const fs = require("fs");
const vm = require("vm");

const loaderPath = process.argv[2] || "skills/assets/min-runtime/godot-loader.js";

function create2DContext() {
	return {
		arcTo() {},
		beginPath() {},
		clearRect() {},
		closePath() {},
		drawImage() {},
		fill() {},
		fillText() {},
		moveTo() {},
	};
}

function createWebGLContext(canvas) {
	return {
		ARRAY_BUFFER: 0x8892,
		BLEND: 0x0be2,
		COLOR_BUFFER_BIT: 0x4000,
		COMPILE_STATUS: 0x8b81,
		CULL_FACE: 0x0b44,
		DEPTH_TEST: 0x0b71,
		FLOAT: 0x1406,
		FRAGMENT_SHADER: 0x8b30,
		LINEAR: 0x2601,
		LINK_STATUS: 0x8b82,
		RGBA: 0x1908,
		STATIC_DRAW: 0x88e4,
		TEXTURE_2D: 0x0de1,
		TEXTURE_MAG_FILTER: 0x2800,
		TEXTURE_MIN_FILTER: 0x2801,
		TEXTURE_WRAP_S: 0x2802,
		TEXTURE_WRAP_T: 0x2803,
		TRIANGLE_STRIP: 0x0005,
		UNSIGNED_BYTE: 0x1401,
		VERTEX_SHADER: 0x8b31,
		CLAMP_TO_EDGE: 0x812f,
		canvas,
		attachShader() {},
		bindBuffer() {},
		bindTexture() {},
		bufferData() {},
		clear() {},
		clearColor() {},
		compileShader() {},
		createBuffer: () => ({}),
		createProgram: () => ({}),
		createShader: () => ({}),
		createTexture: () => ({}),
		deleteBuffer() {},
		deleteProgram() {},
		deleteShader() {},
		deleteTexture() {},
		disable() {},
		drawArrays() {},
		enableVertexAttribArray() {},
		getAttribLocation: () => 0,
		getProgramInfoLog: () => "",
		getProgramParameter: () => true,
		getShaderInfoLog: () => "",
		getShaderParameter: () => true,
		linkProgram() {},
		shaderSource() {},
		texImage2D() {},
		texParameteri() {},
		useProgram() {},
		vertexAttribPointer() {},
		viewport() {},
	};
}

function createHarness({ disabled = false, supported = true } = {}) {
	const gameGlobal = {};
	if (disabled) {
		gameGlobal.__GODOT_DISABLE_WXGLX = true;
	}
	const contextRequests = [];
	const screenCanvas = {
		height: 0,
		style: {},
		width: 0,
		getContext(type) {
			contextRequests.push(type);
			return createWebGLContext(this);
		},
	};
	const offscreenCanvas = {
		height: 0,
		width: 0,
		getContext: () => create2DContext(),
	};
	const windowObject = {
		devicePixelRatio: 1,
		innerHeight: 720,
		innerWidth: 1280,
		addEventListener() {},
		removeEventListener() {},
		requestAnimationFrame() {},
	};
	const context = {
		Float32Array,
		GameGlobal: gameGlobal,
		Image: class {},
		console,
		document: {
			createElement: () => offscreenCanvas,
		},
		window: windowObject,
		wx: {
			env: { isSupportEmscriptenGLX: supported },
			getWindowInfo: () => ({ pixelRatio: 1, windowHeight: 720, windowWidth: 1280 }),
			loadSubpackage({ success }) {
				success();
				return { onProgressUpdate() {} };
			},
		},
	};

	vm.createContext(context);
	vm.runInContext(fs.readFileSync(loaderPath, "utf8"), context);
	new context.GameGlobal.GodotLoader(screenCanvas, {
		barConfig: {
			style: {
				backgroundColor: "#000",
				borderRadius: 0,
				foregroundColor: "#fff",
				height: 10,
				padding: 0,
				width: 100,
			},
		},
		iconConfig: {
			style: { bottom: 0, height: 0, width: 0 },
			visible: false,
		},
		materialConfig: {},
		textConfig: {
			downloadingText: ["loading"],
			firstStartText: "start",
			initText: "init",
			style: { color: "#fff", fontSize: 12 },
		},
	});

	return { contextRequests, gameGlobal };
}

{
	const harness = createHarness();
	assert.deepStrictEqual(harness.contextRequests, ["wxwebgl2"]);
	assert.strictEqual(harness.gameGlobal.__godotMinigameWXGLXEnabled, true);
}

{
	const harness = createHarness({ disabled: true });
	assert.deepStrictEqual(harness.contextRequests, ["webgl2"]);
	assert.strictEqual(harness.gameGlobal.__godotMinigameWXGLXEnabled, false);
}

{
	const harness = createHarness({ supported: false });
	assert.deepStrictEqual(harness.contextRequests, ["webgl2"]);
	assert.strictEqual(harness.gameGlobal.__godotMinigameWXGLXEnabled, false);
}

console.log("Minigame loader GLX context selection tests passed");
