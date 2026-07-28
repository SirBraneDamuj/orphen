@echo off

set "VS_DEV_CMD="

for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) do (
    if exist "%%~P" set "VS_DEV_CMD=%%~P"
)

if not defined VS_DEV_CMD (
    echo Could not find VsDevCmd.bat for Visual Studio 18 or 2022.
    echo Install the Desktop development with C++ workload, then rerun this script.
    exit /b 1
)

call "%VS_DEV_CMD%" -arch=x64 -host_arch=x64 || exit /b 1

if exist "%ProgramFiles%\CMake\bin\cmake.exe" (
    set "PATH=%ProgramFiles%\CMake\bin;%PATH%"
)
