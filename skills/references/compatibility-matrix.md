# Compatibility Matrix

## Supported Base

| Target | Status | How to use |
| --- | --- | --- |
| `origin/4.6` at `a16e481cf424f8e39dc2cdea1a6bdc1e309acdc1` | Supported | Use `scripts/apply_godot_patchset.py` directly |
| Nearby `4.6`-based checkout with small local drift | Best effort | Use `--allow-base-mismatch`, expect conflict resolution, then replay `references/recent-commit-deltas.md` |
| Older `4.5.x` branches | Not drop-in | Use this package as a public reference only |
| Newer upstream than the supported base | Not drop-in | Treat as forward-port work, not as a clean apply |

## Optional Modules

| Optional name | Status | What it adds |
| --- | --- | --- |
| `audio-worker` | Optional | Packages `audio.worker.js` into the template zip if the runtime still references it |
| `dev-types` | Optional | Copies `lib.wx.api.d.ts` for editor tooling and local typing |

## Explicitly Out Of Core Scope

These are intentionally excluded from the default open-source package:

- branding-only changes such as `version.py`
- donor-local docs and unpublished helper scripts
- donor-only test assets

If one of these becomes necessary, it should ship as a separate optional module with its own source files and patch series.
