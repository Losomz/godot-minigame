const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const templateRoots = process.argv.slice(2);
assert.ok(templateRoots.length > 0, "pass one or more template directories");

for (const templateRoot of templateRoots) {
	const gameSource = fs.readFileSync(path.join(templateRoot, "game.js"), "utf8");
	const configImport = gameSource.indexOf("import './glx-config'");
	const loaderImport = gameSource.indexOf("import './godot-loader'");
	assert.ok(configImport >= 0 && configImport < loaderImport, `${templateRoot} must load GLX config before the loader`);

	const configSource = fs.readFileSync(path.join(templateRoot, "glx-config.js"), "utf8");
	const defaultContext = { GameGlobal: {} };
	vm.createContext(defaultContext);
	vm.runInContext(configSource, defaultContext);
	assert.strictEqual(defaultContext.GameGlobal.__GODOT_DISABLE_WXGLX, false, `${templateRoot} must default to GLX enabled`);

	const explicitContext = { GameGlobal: { __GODOT_DISABLE_WXGLX: true } };
	vm.createContext(explicitContext);
	vm.runInContext(configSource, explicitContext);
	assert.strictEqual(explicitContext.GameGlobal.__GODOT_DISABLE_WXGLX, true, `${templateRoot} must preserve an explicit standard-WebGL setting`);
}

console.log("Template GLX startup configuration tests passed");
