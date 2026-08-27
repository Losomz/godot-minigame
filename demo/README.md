# Plugin test project

This project loads a generated addon under `demo/addons/godot-minigame`. The directory is assembled from `plugin/addon` and the native library in `dist/plugin/native`, and is ignored by Git.

From the repository root, prepare a Windows debug build with:

```powershell
pwsh tools/dev/build_demo.ps1
```

Then open `demo/project.godot` in Godot. Re-run the script after changing plugin code.
