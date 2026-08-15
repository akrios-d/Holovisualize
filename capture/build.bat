@echo off
setlocal EnableDelayedExpansion

echo ============================================================
echo  Holovisualize — Capture build
echo ============================================================

:: ── 1. Find vcpkg ────────────────────────────────────────────
set VCPKG_ROOT=
for %%P in (C:\vcpkg %USERPROFILE%\vcpkg %LOCALAPPDATA%\vcpkg) do (
    if exist "%%P\vcpkg.exe" (
        set VCPKG_ROOT=%%P
        goto :found_vcpkg
    )
)

echo [!] vcpkg not found in common locations.
echo     Install it with:
echo       git clone https://github.com/microsoft/vcpkg C:\vcpkg
echo       C:\vcpkg\bootstrap-vcpkg.bat
echo.
set /p VCPKG_ROOT="Enter path to your vcpkg root (or press Enter to abort): "
if "!VCPKG_ROOT!"=="" goto :abort
if not exist "!VCPKG_ROOT!\vcpkg.exe" (
    echo [!] vcpkg.exe not found at !VCPKG_ROOT!
    goto :abort
)

:found_vcpkg
echo [+] vcpkg found at !VCPKG_ROOT!

:: ── 2. Configure (vcpkg manifest mode auto-installs deps) ────
echo [+] Configuring with CMake ...
cmake -B build ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 (
    echo [!] CMake configure failed.
    goto :abort
)

:: ── 3. Build ─────────────────────────────────────────────────
echo [+] Building ...
cmake --build build --config Release --parallel
if errorlevel 1 (
    echo [!] Build failed.
    goto :abort
)

echo.
echo ============================================================
echo  Done!  Binary: build\Release\capture.exe
echo ============================================================
echo.
echo  Usage:
echo    capture.exe [host:port] [session] [sensor_id]
echo    capture.exe localhost:8080 demo sensor0
echo.
echo  Calibration:
echo    capture.exe localhost:8080 demo sensor0 --calibrate
echo ============================================================
goto :end

:abort
echo [!] Build aborted.
exit /b 1

:end
endlocal
