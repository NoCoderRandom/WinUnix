@echo off
setlocal

:: WinUnix - Build Script
:: Requires Visual Studio 2019 or 2022 with C++ workload

set "SCRIPT_DIR=%~dp0"
set "SRC_DIR=%SCRIPT_DIR%src"
set "OUT_DIR=%SCRIPT_DIR%bin"

:: Detect Visual Studio (2022 or 2019)
set "VCVARS="
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
)

if not defined VCVARS (
    echo [ERROR] Visual Studio 2019 or 2022 not found.
    echo Please install "Desktop development with C++" workload.
    exit /b 1
)

call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio environment.
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

set CFLAGS=/nologo /O2 /W3 /EHsc /std:c++17 /MT
set LIBS_PROC=psapi.lib advapi32.lib
set LIBS_PDH=psapi.lib advapi32.lib pdh.lib
set LIBS_SHWAPI=shlwapi.lib

echo Building WinUnix tools...
echo.

set ERRORS=0

call :build uname    ""
call :build free     "%LIBS_PROC%"
call :build df       ""
call :build du       "%LIBS_SHWAPI%"
call :build ps       "%LIBS_PROC%"
call :build pstree   "%LIBS_PROC%"
call :build top      "%LIBS_PDH%"
call :build htop     "%LIBS_PDH%"

echo.
if %ERRORS%==0 (
    echo [OK] All tools built successfully in: %OUT_DIR%
) else (
    echo [WARN] %ERRORS% tool(s) failed to build.
    exit /b 1
)
exit /b 0

:build
set TOOL=%~1
set TOOL_LIBS=%~2
echo   Building %TOOL%...
cl %CFLAGS% "%SRC_DIR%\%TOOL%.cpp" /Fe:"%OUT_DIR%\%TOOL%.exe" %TOOL_LIBS% >"%OUT_DIR%\%TOOL%_build.log" 2>&1
if errorlevel 1 (
    echo   [FAIL] %TOOL% - see %OUT_DIR%\%TOOL%_build.log
    set /a ERRORS+=1
) else (
    del "%OUT_DIR%\%TOOL%_build.log" 2>nul
    del "%OUT_DIR%\%TOOL%.obj"       2>nul
    echo   [OK]   %TOOL%.exe
)
exit /b 0
