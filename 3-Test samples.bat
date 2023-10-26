@echo off

set FRAME_NUM=100
set DIR_DATA=_Data

set DIR_BIN=_Bin\Release

if not exist "%DIR_BIN%" (
    set DIR_BIN=_Bin\Debug
)

if not exist "%DIR_BIN%" (
    echo The project is not compiled!
    pause
    exit /b
)
echo Running samples from '%DIR_BIN%'...
echo.

:: API independent samples
"%DIR_BIN%\DeviceInfo.exe"
if %ERRORLEVEL% equ 0 (
    echo =^> OK
) else (
    echo =^> FAILED!
)
echo.

:: API dependent samples
call :TestSample Clear
call :TestSample Triangle
call :TestSample SceneViewer
call :TestSample Readback
call :TestSample AsyncCompute
call :TestSample MultiThreading
call :TestSample MultiGPU
call :TestSample RayTracingTriangle
call :TestSample RayTracingBoxes
call :TestSample Wrapper

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
