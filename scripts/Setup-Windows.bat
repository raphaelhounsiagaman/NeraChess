@echo off
setlocal

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=vs2026"

pushd "%~dp0\.."
vendor\Premake\Windows\premake5.exe --fatal --file=Build-NeraChess.lua %ACTION%
set "RESULT=%ERRORLEVEL%"
popd

exit /b %RESULT%
