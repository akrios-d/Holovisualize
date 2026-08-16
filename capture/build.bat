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

:: ── 1b. Find cmake ────────────────────────────────────────────
set CMAKE_EXE=
where cmake >nul 2>&1
if not errorlevel 1 set CMAKE_EXE=cmake
if "!CMAKE_EXE!"=="" if exist "%LOCALAPPDATA%\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe" set CMAKE_EXE=%LOCALAPPDATA%\Programs\CLion\bin\cmake\win\x64\bin\cmake.exe
if "!CMAKE_EXE!"=="" if exist "C:\Program Files\CMake\bin\cmake.exe" set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
if "!CMAKE_EXE!"=="" if exist "C:\Program Files (x86)\CMake\bin\cmake.exe" set CMAKE_EXE=C:\Program Files (x86)\CMake\bin\cmake.exe
if "!CMAKE_EXE!"=="" (
    echo [!] cmake not found. Install CMake or add it to PATH.
    goto :abort
)
echo [+] cmake: !CMAKE_EXE!

:: ── 2. Detect sensor ──────────────────────────────────────────
set KINECT_V1_FLAG=OFF
set KINECT_SDK_FLAG=OFF

if /i "%1"=="v1" (
    set KINECT_V1_FLAG=ON
    echo [+] Building for Kinect v1 ^(libfreenect^)
) else if /i "%1"=="lib" (
    echo [+] Building for Kinect v2 ^(libfreenect2, forced^)
) else if defined KINECTSDK20_ROOT (
    set KINECT_SDK_FLAG=ON
    echo [+] Kinect SDK 2.0 found at !KINECTSDK20_ROOT!
) else if exist "C:\Program Files\Microsoft SDKs\Kinect\v2.0_1409" (
    set KINECT_SDK_FLAG=ON
    echo [+] Kinect SDK 2.0 found at default path
) else (
    echo [i] No Kinect SDK found, using libfreenect2
)

:: ── 3. Clean previous build ───────────────────────────────────
if exist build (
    echo [+] Cleaning previous build...
    rmdir /s /q build
)

:: ── 4. Configure ──────────────────────────────────────────────
echo [+] Configuring with CMake ...
"!CMAKE_EXE!" -B build ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DCMAKE_PREFIX_PATH="!CD!\vcpkg_installed\x64-windows" ^
    -DHOLOVISUALIZE_KINECT_V1=!KINECT_V1_FLAG! ^
    -DHOLOVISUALIZE_KINECT_SDK=!KINECT_SDK_FLAG! ^
    -DHOLOVISUALIZE_OPENCV=OFF
if errorlevel 1 (
    echo [!] CMake configure failed.
    goto :abort
)

:: ── 5. Build ─────────────────────────────────────────────────
echo [+] Building ...
"!CMAKE_EXE!" --build build --config Release --parallel
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
