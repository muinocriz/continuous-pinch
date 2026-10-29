@echo off
setlocal

echo [1/2] Compiling Windows resources...
rc /nologo app.rc
if errorlevel 1 (
    echo.
    echo Resource compile failed.
    exit /b 1
)

echo [2/2] Building ContinuousPinch.exe...
cl /nologo /std:c++17 /EHsc /O2 /utf-8 ^
src\ContinuousPinch.cpp ^
app.res ^
user32.lib ^
comctl32.lib ^
winmm.lib ^
shell32.lib ^
advapi32.lib ^
/Fe:ContinuousPinch.exe ^
/link /SUBSYSTEM:WINDOWS

if errorlevel 1 (
    echo.
    echo Build failed.
    exit /b 1
)

echo.
echo Build succeeded: ContinuousPinch.exe
echo.
pause
