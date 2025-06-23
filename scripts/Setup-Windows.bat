@echo off

pushd ..
vendor\Premake\Windows\premake5.exe --file=Build-NeraChess.lua vs2022
popd
pause