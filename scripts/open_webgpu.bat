@echo off
rem Launch EVEngine WebGPU demo in the default browser.
rem Serves build/webgpu-web/src/engine with COOP/COEP headers (required for
rem Emscripten pthreads / SharedArrayBuffer) and opens eve.html.

setlocal
set "ROOT=%~dp0.."
set "ENGINE_DIR=%ROOT%\build\webgpu-web\src\engine"

if not exist "%ENGINE_DIR%\eve.js" (
    echo eve.js not found. Build first:
    echo   emcmake cmake -B build/webgpu-web -G Ninja -DCMAKE_BUILD_TYPE=Release
    echo   cmake --build build/webgpu-web
    exit /b 1
)

echo Checking for an existing server on 8090...
powershell -NoProfile -Command "try { (Invoke-WebRequest -Uri 'http://127.0.0.1:8090/eve.js' -Method Head -UseBasicParsing -TimeoutSec 2).StatusCode } catch { 'down' }" > "%TEMP%\eve_srv.txt"
set /p SRV=<"%TEMP%\eve_srv.txt"
if "%SRV%"=="200" (
    echo Server already running on 8090.
) else (
    echo Starting COOP/COEP server on 8090...
    start "EVEngine WebGPU server" python "%ROOT%\build\serve_webgpu.py"
    timeout /t 2 /nobreak >nul
)

echo Opening http://127.0.0.1:8090/eve.html ...
start "" "http://127.0.0.1:8090/eve.html"
endlocal
