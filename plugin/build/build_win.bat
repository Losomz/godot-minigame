@echo off
setlocal
cd /d "%~dp0\..\.."

scons -f plugin/build/SConstruct platform=windows arch=x86_64 target=template_release embed_resources=yes || exit /b 1
