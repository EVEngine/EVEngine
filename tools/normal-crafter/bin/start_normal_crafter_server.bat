@echo off
REM Start the NormalCrafter GPU server (Windows). Requires NVIDIA GPU + deps.
REM Usage: start_normal_crafter_server.bat [--port 8000]
cd /d "%~dp0.."
python -m server %*
