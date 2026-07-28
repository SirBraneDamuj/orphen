@echo off
setlocal

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"
set "PORT_DIR=%~dp0."
set "BUILD_DIR=%~dp0build\msvc-%CONFIG%"

call "%~dp0msvc-env.bat" || exit /b 1

cmake -S "%PORT_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE="%CONFIG%" -DORPHEN_PORT_FETCH_SDL2=ON || exit /b 1
cmake --build "%BUILD_DIR%"
