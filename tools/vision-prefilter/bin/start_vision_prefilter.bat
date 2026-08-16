@echo off
REM Start the vision pre-filter service in the background (Windows).
REM Writes PID to vision-prefilter.pid; logs to vision-prefilter.log.
setlocal
set PORT=%VISION_PREFILTER_PORT%
if "%PORT%"=="" set PORT=8531

if exist vision-prefilter.pid (
  for /f %%P in (vision-prefilter.pid) do (
    tasklist /FI "PID eq %%P" 2>NUL | find "%%P" >NUL
    if not errorlevel 1 (
      echo vision-prefilter already running (PID %%P, port %PORT%).
      exit /b 0
    )
  )
)

start "vision-prefilter" /B python -m vision_prefilter --host 127.0.0.1 --port %PORT% --eager-load >> vision-prefilter.log 2>&1
if errorlevel 1 (
  echo Failed to start vision-prefilter. See vision-prefilter.log
  exit /b 1
)

REM Capture the child PID via wmic into a pidfile.
for /f "tokens=2 delims=," %%P in ('wmic process where "name='python.exe' and commandline like '%%vision_prefilter%%'" get ProcessId /value ^| findstr /i "ProcessId"') do echo %%P > vision-prefilter.pid

echo vision-prefilter starting on http://127.0.0.1:%PORT%  (log: vision-prefilter.log)
exit /b 0
