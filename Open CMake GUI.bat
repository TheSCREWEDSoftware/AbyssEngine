@echo off
setlocal
set "SRC=%~dp0."
set "BUILD=%~dp0build"

rem --- Find vcpkg: VCPKG_ROOT, C:\vcpkg, or the copy bundled with Visual Studio 2022 ---
set "VCPKG="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG=%VCPKG_ROOT%"
if not defined VCPKG if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG=C:\vcpkg"
for %%E in (Community Professional Enterprise BuildTools) do (
    if not defined VCPKG if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\vcpkg"
)
if not defined VCPKG (
    echo vcpkg not found. Install the "vcpkg package manager" component in the Visual Studio Installer,
    echo or run: git clone https://github.com/microsoft/vcpkg C:\vcpkg ^&^& C:\vcpkg\bootstrap-vcpkg.bat
    pause
    exit /b 1
)
echo Using vcpkg at: %VCPKG%

rem --- Wipe an old configuration that was made without vcpkg ---
if exist "%BUILD%\CMakeCache.txt" (
    findstr /c:"vcpkg.cmake" "%BUILD%\CMakeCache.txt" >nul || (
        echo Removing old CMake cache that was configured without vcpkg...
        del /q "%BUILD%\CMakeCache.txt"
        rmdir /s /q "%BUILD%\CMakeFiles" 2>nul
    )
)

rem --- First configure (downloads/builds SDL2, FFmpeg, libarchive, zlib - slow the first time) ---
cmake -S "%SRC%" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DGITHUB_ACTIONS=1
if errorlevel 1 (
    echo.
    echo CMake configure failed - see the messages above.
    pause
    exit /b 1
)

start "" cmake-gui -S "%SRC%" -B "%BUILD%"
