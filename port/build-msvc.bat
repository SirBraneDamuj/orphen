@echo off
setlocal

set "CONFIG=Debug"
if not "%~1"=="" set "CONFIG=%~1"
set "PORT_DIR=%~dp0."
set "BUILD_DIR=%~dp0build\msvc-%CONFIG%"

call "%~dp0msvc-env.bat" || exit /b 1

cmake -S "%PORT_DIR%" -B "%BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE="%CONFIG%" -DORPHEN_PORT_FETCH_SDL2=ON || exit /b 1
cmake --build "%BUILD_DIR%" || exit /b 1

if /i "%CONFIG%"=="Debug" (
    echo.
    echo NOTE: this is a Debug build -- about 8x slower than Release ^(40.5 vs
    echo       4.9 ms/frame on s01_e024^), and slower still once the frame
    echo       accumulator starts running catch-up simulation steps. Fine for
    echo       debugging; do not judge the frame rate by it.
    echo       Build one to play in with:  build-msvc.bat Release
)
