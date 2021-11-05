@echo off

set FRAME_NUM=100
set DIR_DATA=_Data

set DIR_BIN=_Build\Release

if not exist "%DIR_BIN%" (
    set DIR_BIN=_Build\Debug
)

if not exist "%DIR_BIN%" (
    echo The project is not compiled!
    pause
    exit /b
)
echo Running samples from '%DIR_BIN%'...
echo.

call :TestSample 00_Clear
call :TestSample 01_Triangle
call :TestSample 02_SceneViewer
call :TestSample 03_Readback
call :TestSample 04_AsyncCompute
call :TestSample 05_Multithreading
call :TestSample 06_MultiGPU
call :TestSample 07_RayTracing_Triangle
call :TestSample 08_RayTracing_Boxes

pause

exit /b

::========================================================================================
:TestSample

echo %1 [D3D11]
"%DIR_BIN%\%1.exe" --api=D3D11 --frameNum=%FRAME_NUM%
if %ERRORLEVEL% equ 0 (
    echo =^> OK
) else (
    echo =^> FAILED!
)
echo.

echo %1 [D3D12]
"%DIR_BIN%\%1.exe" --api=D3D12 --frameNum=%FRAME_NUM%
if %ERRORLEVEL% equ 0 (
    echo =^> OK
) else (
    echo =^> FAILED!
)
echo.

echo %1 [VULKAN]
"%DIR_BIN%\%1.exe" --api=VULKAN --frameNum=%FRAME_NUM%
if %ERRORLEVEL% equ 0 (
    echo =^> OK
) else (
    echo =^> FAILED!
)
echo.

exit /b
