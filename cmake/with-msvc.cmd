@echo off
chcp 65001 >nul
setlocal
set "EVE_MSVC_ARGS=%*"
if "%~1"=="--arm64" goto :arm64
goto :x64
:arm64
rem The option is a fixed prefix; strip it from the command forwarded below.
set "EVE_MSVC_ARGS=%EVE_MSVC_ARGS:~8%"
set "EVE_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.ARM64"
set "EVE_VCVARS=vcvarsarm64.bat"
goto :setup
:x64
set "EVE_VC_COMPONENT=Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
set "EVE_VCVARS=vcvars64.bat"
:setup
rem Prefer English cl.exe messages when the 1033 language pack is installed.
set "VSLANG=1033"
rem Let CMake resolve the UTF-8 compiler wrapper by command name. CMake rejects
rem compiler paths containing a relative directory component.
set "PATH=%~dp0;%PATH%"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo error: vswhere.exe not found at "%VSWHERE%" >&2
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires %EVE_VC_COMPONENT% -find VC\Auxiliary\Build\%EVE_VCVARS%`) do (
  call "%%i" >nul
  if errorlevel 1 exit /b 1
  goto :run
)
echo error: %EVE_VCVARS% not found via vswhere >&2
exit /b 1
:run
%EVE_MSVC_ARGS%
exit /b %ERRORLEVEL%
