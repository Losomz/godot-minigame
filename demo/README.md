# Plugin test project

This project loads the canonical addon from `../../addons/godot-minigame` through a local directory junction. The junction and native binaries are ignored by Git.

From the repository root, prepare a Windows debug build with:

```powershell
pwsh tools/dev/build_demo.ps1
```

Then open `demo/project.godot` in Godot. Re-run the script after changing native plugin code.
