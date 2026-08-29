@echo off
setlocal
set LOUDNESS_DISABLE=%~1
set USBSTATISTICS_DISABLE=%~2
if "%LOUDNESS_DISABLE%"=="" set LOUDNESS_DISABLE=0
if "%USBSTATISTICS_DISABLE%"=="" set USBSTATISTICS_DISABLE=0
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { Set-Location '%~dp0'; . .\vcvars64.ps1; make run-test CC=cl LOUDNESS_DISABLE=%LOUDNESS_DISABLE% USBSTATISTICS_DISABLE=%USBSTATISTICS_DISABLE% }"
exit /b %ERRORLEVEL%
