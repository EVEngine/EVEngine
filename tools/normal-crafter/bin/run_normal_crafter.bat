@echo off
REM Run NormalCrafter CPU client (Windows): sequence -> temporally consistent normals.
REM Usage: run_normal_crafter.bat --server http://host:8000 --input <video|frames_dir> --output <out_dir>
cd /d "%~dp0.."
python -m normal_crafter %*
