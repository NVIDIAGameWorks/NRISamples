@echo off

rd /q /s "_Bin"
rd /q /s "_Build"
rd /q /s "_Data"
rd /q /s "_Shaders"

rd /q /s "build"

cd "External/NRIFramework"
call "4-Clean.bat"
cd "../.."
