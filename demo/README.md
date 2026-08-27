# Plugin test project

This project loads a generated addon under `demo/addons/godot-minigame`. The directory is assembled from `plugin/addon` and the native library in `dist/plugin/native`, and is ignored by Git.

From the repository root, prepare a Windows debug build with:

```powershell
pwsh tools/dev/build_demo.ps1
```

To test plugin replacement without publishing a Release, run `python tools/product/product.py package-plugin`, then select `dist/plugin/godot-minigame-plugin-<version>.zip` from the plugin settings' `本地插件包` channel. Local packages may upgrade, overwrite the same version, or downgrade after explicit confirmation.

Then open `demo/project.godot` in Godot. Re-run the script after changing plugin code.
