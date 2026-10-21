@echo off
rem Checks/installs tools and libraries, rebuilds and starts CppDetector.
rem Options are passed to rebuild-and-run.ps1, e.g.: rebuild-and-run.cmd -NoRun  ^|  -Clean  ^|  -CheckOnly
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0rebuild-and-run.ps1" %*
exit /b %ERRORLEVEL%
