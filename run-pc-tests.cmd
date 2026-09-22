@echo off
setlocal
set LOUDNESS_DISABLE=%~1
set USBSTATISTICS_DISABLE=%~2
if "%LOUDNESS_DISABLE%"=="" set LOUDNESS_DISABLE=0
if "%USBSTATISTICS_DISABLE%"=="" set USBSTATISTICS_DISABLE=0
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
make run-test CC=cl LOUDNESS_DISABLE=%LOUDNESS_DISABLE% USBSTATISTICS_DISABLE=%USBSTATISTICS_DISABLE%
exit /b %ERRORLEVEL%
