---
name: godot-wechat-minigame-adapter
description: Maintain and apply this repository's Godot 4.5 WeChat Mini Game adapter. Use when editing the engine patchset, Web runtime sources, host loader, trim profiles, or adapter tests.
---

# Godot WeChat Mini Game Adapter

Use the official Godot checkout recorded by the repository's `godot/` submodule and the maintained adaptation baseline under `adapter/`. Do not treat this Skill directory as an adapter source package, and do not clone another Godot checkout into an Agent temporary directory.

## Source Locations

- Official Godot source: `godot/`
- Host runtime: `adapter/assets/min-runtime/`
- Build profiles: `adapter/configs/`
- Engine patches: `adapter/patches/`
- Maintenance references: `adapter/references/`
- Engine source overlays: `adapter/sources/`
- Adapter tools: `adapter/scripts/`
- Tests: `adapter/tests/`

The `godot` gitlink is the source of truth for the supported upstream revision. The verified Emscripten version is `4.0.10`.

## Rules

- Keep real project code under `adapter/`, never under this Skill directory.
- Preserve the official `godot/` submodule as the engine baseline.
- Keep runtime, engine overlay, patch, and test changes synchronized.
- Do not edit generated template `engine/godot.js` or `godot.wasm.br` as the primary source of a fix.
- Do not initialize the optional `godot/` submodule unless engine development or template compilation requires it.
- Do not run a full engine build unless the user explicitly requests compilation or build verification.

## Apply

```powershell
python adapter/scripts/apply_godot_patchset.py --include-optional export-api
```

Read `adapter/references/runtime-shell.md` for the host contract and `adapter/references/validation-checklist.md` before declaring an adapter change complete.
