@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cd /d "%~dp0"
if not exist build mkdir build
cd build
if not exist build.ninja cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. >nul 2>&1
if errorlevel 1 (echo CONFIG_FAIL & exit /b 1)
cmake --build . 2>&1
if errorlevel 1 (echo BUILD_FAIL & exit /b 1)

rem Inject icon resources (rc-compiled ICO lacks RT_GROUP_ICON)
if exist "%~dp0_tools\inject_icon.exe" (
    "%~dp0_tools\inject_icon.exe" "ArchiveCleaner.exe" "%~dp0release\assets\icon.ico" >nul 2>&1
)

echo BUILD_OK
