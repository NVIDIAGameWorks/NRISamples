#!/bin/sh

rm -rf "_Build"
rm -rf "_Compiler"
rm -rf "_Data"
rm -rf "_Shaders"
rm -rf "External/NRIFramework/External/Assimp"
rm -rf "External/NRIFramework/External/Detex"
rm -rf "External/NRIFramework/External/Glfw"
rm -rf "External/NRIFramework/External/ImGui"

rm -rf "build"

cd "External/NRIFramework"
source "4-Clean.sh"
cd "../.."
