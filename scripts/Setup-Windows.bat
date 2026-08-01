@echo off

pushd ..
vendor\Premake\Windows\premake5.exe --file=Build-NeraChess.lua vs2026
popd
pause