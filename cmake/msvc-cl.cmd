@echo off
rem Used as CMAKE_C/CXX_COMPILER under Ninja+sccache so cl.exe is a child of
rem this cmd (CP65001), not of sccache's ACP pipe.
chcp 65001 >nul
set "VSLANG=1033"
cl.exe %*
exit /b %ERRORLEVEL%
