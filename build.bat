@echo off
setlocal

rem Build Continuous Pinch with MSVC.
rem Run from a "x64 Native Tools Command Prompt for VS".
rem Pass "nopause" as the first argument to skip the final pause (e.g. in CI).

cd /d "%~dp0"

if not exist build mkdir build

echo [1/2] Compiling Windows resources...
rc /nologo /fo build\app.res app.rc
if errorlevel 1 (
    echo.
    echo Resource compile failed.
    goto :fail
)

echo [2/2] Building ContinuousPinch.exe...
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /W4 ^
   /DUNICODE /D_UNICODE ^
   /Fobuild\ ^
   src\*.cpp ^
   build\app.res ^
   user32.lib comctl32.lib winmm.lib shell32.lib advapi32.lib gdi32.lib ^
   /Fe:ContinuousPinch.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO

if errorlevel 1 (
    echo.
    echo Build failed.
    goto :fail
)

echo.
echo Build succeeded: ContinuousPinch.exe
if /i not "%~1"=="nopause" pause
exit /b 0

:fail
if /i not "%~1"=="nopause" pause
exit /b 1
