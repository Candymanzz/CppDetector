@echo off
rem Starts CppDetector (no build). Options are passed to run.ps1, e.g.: run.cmd -Port 8001
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
exit /b %ERRORLEVEL%
