@echo off
setlocal

call "%~dp0msvc-env.bat" || exit /b 1

echo.
echo MSVC developer environment is active.
echo VSCMD_VER=%VSCMD_VER%

echo.
where cl || exit /b 1
cl 2>&1 | findstr /C:"Version"

echo.
where cmake || exit /b 1
cmake --version
