#!/bin/bash

rm -rf "_Bin"
rm -rf "_Build"
rm -rf "_Data"
rm -rf "_Shaders"

rm -rf "build"

cd "External/NRIFramework"
source "4-Clean.sh"
cd "../.."
