@echo off
setlocal

set "WASM=bin\.web_zip\godot.wasm"
set "OUTPUT=%WASM%.br"

where brotli.exe >nul 2>&1 || (
    echo ERROR: brotli.exe was not found in PATH.
    exit /b 1
)

if not exist "%WASM%" (
    echo ERROR: %WASM% does not exist. Build the Web template first.
    exit /b 1
)

if exist "%OUTPUT%" del /F /Q "%OUTPUT%"
brotli.exe --force "%WASM%" || exit /b 1
node godot_process.js || exit /b 1

if not exist "%OUTPUT%" (
    echo ERROR: Brotli output was not created: %OUTPUT%
    exit /b 1
)

echo Created %OUTPUT% and patched bin\.web_zip\godot.js
