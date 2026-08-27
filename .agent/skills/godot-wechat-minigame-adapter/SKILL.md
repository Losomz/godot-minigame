---
name: godot-wechat-minigame-adapter
description: Maintain and apply this repository's version-locked Godot 4.6 WeChat Mini Game adapter. Use when editing its engine patchset, Web runtime overlays, host loader, adapter tests, or migration references.
---

# Godot 4.6 WeChat Mini Game Adapter

Use the checkout recorded by the repository's `godot/` submodule and the maintained adaptation sources under `adapter/`. Do not treat this Skill directory as an adapter source package, and do not clone another Godot checkout into an Agent temporary directory.

## Source Locations

- Godot source and build tree: `godot/`
- Host runtime: `adapter/assets/min-runtime/`
- Engine patches: `adapter/patches/`
- Maintenance references: `adapter/references/`
- Engine source overlays: `adapter/sources/`
- Adapter tools: `adapter/scripts/`
- Tests: `adapter/tests/`

The `godot` gitlink is the source of truth for the supported official revision. The adapter is experimental and does not define a production template build contract.

## Rules

- Keep production code under `adapter/` or `godot/`, never under this Skill directory.
- Preserve the official Godot base recorded in `adapter/patches/manifest.json`.
- Keep runtime, engine overlay, patch, and test changes synchronized.
- Do not claim WXGLX support from loader-side tests alone; this bundle does not ship the complete GLX engine build contract.
- Do not edit generated template `engine/godot.js` or `godot.wasm.br` as the primary source of a fix.
- Do not initialize the optional `godot/` submodule unless engine work requires it.
- Do not run a full engine build unless the user explicitly requests compilation or build verification.

## Apply

```powershell
python adapter/scripts/apply_godot_patchset.py godot
```

Read `adapter/README.md`, `adapter/references/compatibility-matrix.md`, and `adapter/references/validation-checklist.md` before declaring an adapter change complete.
