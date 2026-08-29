@echo off
chcp 65001 >nul
setlocal
rem Prefer English cl.exe messages when the 1033 language pack is installed.
set "VSLANG=1033"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo error: vswhere.exe not found at "%VSWHERE%" >&2
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
  call "%%i" >nul
  goto :run
)
echo error: vcvars64.bat not found via vswhere >&2
exit /b 1
:run
%*
exit /b %ERRORLEVEL%
