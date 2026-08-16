@echo off
REM Start the vision pre-filter stack in the background (Windows).
REM   1. llama-server (llama.cpp) serving a Qwen2-VL-2B GGUF model
REM   2. the vision-prefilter FastAPI gateway in front of it
REM Usage: bin\start_vision_prefilter.bat path\to\model.gguf
setlocal

set MODEL_GGUF=%1
if "%MODEL_GGUF%"=="" set MODEL_GGUF=%VISION_PREFILTER_GGUF%
if "%MODEL_GGUF%"=="" (
  echo ERROR: provide a Qwen2-VL-2B GGUF model path ^(arg or VISION_PREFILTER_GGUF^).
  exit /b 1
)
if not exist "%MODEL_GGUF%" (
  echo ERROR: model file not found: %MODEL_GGUF%
  exit /b 1
)

if "%LLAMA_PORT%"=="" set LLAMA_PORT=8080
if "%VISION_PREFILTER_PORT%"=="" set VISION_PREFILTER_PORT=8531

if exist llama-server.pid (
  for /f %%P in (llama-server.pid) do (
    tasklist /FI "PID eq %%P" 2>NUL | find "%%P" >NUL
    if not errorlevel 1 goto gw
  )
)
start "llama-server" /B llama-server -m "%MODEL_GGUF%" --host 127.0.0.1 --port %LLAMA_PORT% -ngl 99 --no-warmup >> llama-server.log 2>&1
for /f "tokens=2 delims=," %%P in ('wmic process where "name='llama-server.exe' and commandline like '%%%MODEL_GGUF%'" get ProcessId /value ^| findstr /i "ProcessId"') do echo %%P > llama-server.pid
echo llama-server starting on http://127.0.0.1:%LLAMA_PORT%  ^(log: llama-server.log^)

:gw
if exist vision-prefilter.pid (
  for /f %%P in (vision-prefilter.pid) do (
    tasklist /FI "PID eq %%P" 2>NUL | find "%%P" >NUL
    if not errorlevel 1 (
      echo vision-prefilter already running ^(PID %%P, port %VISION_PREFILTER_PORT%^).
      exit /b 0
    )
  )
)
set VISION_PREFILTER_BACKEND=http://127.0.0.1:%LLAMA_PORT%
start "vision-prefilter" /B python -m vision_prefilter --host 127.0.0.1 --port %VISION_PREFILTER_PORT% --backend http://127.0.0.1:%LLAMA_PORT% >> vision-prefilter.log 2>&1
for /f "tokens=2 delims=," %%P in ('wmic process where "name='python.exe' and commandline like '%%vision_prefilter%%'" get ProcessId /value ^| findstr /i "ProcessId"') do echo %%P > vision-prefilter.pid
echo vision-prefilter starting on http://127.0.0.1:%VISION_PREFILTER_PORT%  ^(log: vision-prefilter.log^)
exit /b 0
