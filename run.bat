@echo off
REM ──────────────────────────────────────────────────────────────────────────
REM CrystalClockVK — Windows Launcher
REM Vulkan runs natively on Windows — no MoltenVK needed.
REM ──────────────────────────────────────────────────────────────────────────

set SCRIPT_DIR=%~dp0
set BIN_DIR=%SCRIPT_DIR%bin
set BINARY=%BIN_DIR%\CrystalClockVK.exe

REM Disable ReShade injection — it breaks our Vulkan dynamic-rendering pipeline.
set VK_LOADER_LAYERS_DISABLE=*ReShade*,*reshade*

if not exist "%BINARY%" (
    echo [!] Binary not found at %BINARY%
    echo     Run: cmake -B build -DCMAKE_BUILD_TYPE=Release ^&^& cmake --build build --config Release
    exit /b 1
)

echo [*] Launching CrystalClockVK
echo     Binary: %BINARY%
echo.

"%BINARY%" %*
echo.
echo [*] Exit code: %ERRORLEVEL%
