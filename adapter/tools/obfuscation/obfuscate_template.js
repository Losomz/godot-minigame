"use strict";

const fs = require("fs");
const path = require("path");
const JavaScriptObfuscator = require("javascript-obfuscator");

const FILES = ["game.js", "godot-loader.js", "engine/game.js"];

function fail(message) {
    console.error(message);
    process.exit(1);
}

function parseArgs(argv) {
    const positional = [];
    let seed;

    for (let index = 0; index < argv.length; index += 1) {
        if (argv[index] === "--seed") {
            seed = Number(argv[index + 1]);
            index += 1;
        } else {
            positional.push(argv[index]);
        }
    }

    if (positional.length !== 2) {
        fail("Usage: node obfuscate_template.js <source-template> <output-template> [--seed <integer>]");
    }
    if (seed !== undefined && (!Number.isSafeInteger(seed) || seed < 0)) {
        fail("--seed must be a non-negative safe integer");
    }

    return {
        source: path.resolve(positional[0]),
        output: path.resolve(positional[1]),
        seed,
    };
}

const args = parseArgs(process.argv.slice(2));
if (!fs.statSync(args.source, { throwIfNoEntry: false })?.isDirectory()) {
    fail(`Source template does not exist: ${args.source}`);
}
if (fs.existsSync(args.output)) {
    fail(`Output path already exists: ${args.output}`);
}
if (args.output.startsWith(`${args.source}${path.sep}`)) {
    fail("Output path must not be inside the source template");
}

const configPath = path.join(__dirname, "config.json");
const config = JSON.parse(fs.readFileSync(configPath, "utf8"));
if (args.seed !== undefined) {
    config.seed = args.seed;
}

for (const relativePath of FILES) {
    const sourcePath = path.join(args.source, relativePath);
    if (!fs.statSync(sourcePath, { throwIfNoEntry: false })?.isFile()) {
        fail(`Required script does not exist: ${sourcePath}`);
    }
}

fs.cpSync(args.source, args.output, { recursive: true, errorOnExist: true });

console.log(`Obfuscation seed: ${config.seed}`);
for (const relativePath of FILES) {
    const outputPath = path.join(args.output, relativePath);
    const source = fs.readFileSync(outputPath, "utf8");
    const result = JavaScriptObfuscator.obfuscate(source, {
        ...config,
        inputFileName: relativePath.replaceAll("\\", "/"),
        identifiersPrefix: relativePath.replace(/[^a-z0-9]/gi, "_") + "_",
    });
    const obfuscated = result.getObfuscatedCode();
    const temporaryPath = `${outputPath}.tmp`;
    fs.writeFileSync(temporaryPath, obfuscated, "utf8");
    fs.renameSync(temporaryPath, outputPath);
    console.log(`${relativePath}: ${Buffer.byteLength(source)} -> ${Buffer.byteLength(obfuscated)} bytes`);
}

console.log(`Obfuscated template: ${args.output}`);
