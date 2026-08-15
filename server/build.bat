@echo off
setlocal EnableDelayedExpansion

echo ============================================================
echo  Holovisualize — Server build (native Windows)
echo  Tip: use Docker for production (docker compose up --build)
echo ============================================================

:: Find vcpkg
set VCPKG_ROOT=
for %%P in (C:\vcpkg %USERPROFILE%\vcpkg %LOCALAPPDATA%\vcpkg) do (
    if exist "%%P\vcpkg.exe" (
        set VCPKG_ROOT=%%P
        goto :found_vcpkg
    )
)
echo [!] vcpkg not found. See capture\build.bat for instructions.
goto :abort

:found_vcpkg
echo [+] vcpkg at !VCPKG_ROOT!

:: Install ixwebsocket if not already present
"!VCPKG_ROOT!\vcpkg" install ixwebsocket:x64-windows

cmake -B build ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows
if errorlevel 1 goto :abort

cmake --build build --config Release --parallel
if errorlevel 1 goto :abort

echo.
echo [+] Done: build\Release\server.exe
echo     Run: build\Release\server.exe [ws_port] [voxel_res]
goto :end

:abort
echo [!] Build aborted.
exit /b 1

:end
endlocal
