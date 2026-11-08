@echo off
rem ---------------------------------------------------------------------------
rem  Raven - generate-cmake.bat
rem  Generates the CMake build tree: a Visual Studio solution by default.
rem
rem    generate-cmake.bat [options] [-- extra cmake arguments]
rem
rem    --build-dir DIR     build tree                      (default: build)
rem    --generator NAME    CMake generator                 (default: the newest
rem                        Visual Studio that is installed)
rem    --arch NAME         x64, Win32 or ARM64             (Visual Studio only)
rem    --shared            build raven.dll instead of a static library
rem    --no-examples       do not build the examples
rem    --no-tests          do not build the tests
rem    --no-vulkan         leave the Vulkan backend out
rem    --fresh             delete the build tree first
rem    --open              open the generated solution in Visual Studio
rem    --no-pause          do not wait for a key when the generation fails
rem
rem  Examples:
rem    generate-cmake.bat
rem    generate-cmake.bat --shared --open
rem    generate-cmake.bat --generator Ninja -- -DCMAKE_BUILD_TYPE=Debug
rem
rem  The Vulkan SDK is not required: without it the Vulkan headers are fetched
rem  when the build tree is generated (see README.md).
rem ---------------------------------------------------------------------------
setlocal EnableExtensions

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "BUILD_DIR=%ROOT%\build"
set "GENERATOR="
set "ARCH="
set "SHARED=OFF"
set "EXAMPLES=ON"
set "TESTS=ON"
set "VULKAN=ON"
set "FRESH=0"
set "OPEN=0"
set "EXTRA="
set "NO_PAUSE=0"
if defined RAVEN_NO_PAUSE set "NO_PAUSE=1"

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--build-dir"   ( set "BUILD_DIR=%~f2" & shift & shift & goto parse )
if /I "%~1"=="--generator"   ( set "GENERATOR=%~2" & shift & shift & goto parse )
if /I "%~1"=="--arch"        ( set "ARCH=%~2" & shift & shift & goto parse )
if /I "%~1"=="--shared"      ( set "SHARED=ON" & shift & goto parse )
if /I "%~1"=="--no-examples" ( set "EXAMPLES=OFF" & shift & goto parse )
if /I "%~1"=="--no-tests"    ( set "TESTS=OFF" & shift & goto parse )
if /I "%~1"=="--no-vulkan"   ( set "VULKAN=OFF" & shift & goto parse )
if /I "%~1"=="--fresh"       ( set "FRESH=1" & shift & goto parse )
if /I "%~1"=="--open"        ( set "OPEN=1" & shift & goto parse )
if /I "%~1"=="--no-pause"    ( set "NO_PAUSE=1" & shift & goto parse )
if /I "%~1"=="--help"        goto usage
if /I "%~1"=="-h"            goto usage
if "%~1"=="--"               goto extra
echo [raven] unknown option: %~1
echo         ^(arguments for cmake go after "--"^)
echo.
goto usage

:extra
rem  Everything after "--" goes to cmake as it was typed. It is taken from the
rem  whole command line: cmd splits arguments at "=", which would break -DNAME=VALUE.
set "ALL= %* "
set "EXTRA=%ALL:* -- =%"

:parsed
if "%BUILD_DIR:~-1%"=="\" set "BUILD_DIR=%BUILD_DIR:~0,-1%"

where cmake >nul 2>nul
if errorlevel 1 (
	echo [raven] cmake was not found in the PATH.
	echo         Install CMake 3.16 or newer from https://cmake.org/download, or start
	echo         this script from a "Developer Command Prompt for Visual Studio".
	goto failed
)

if not "%FRESH%"=="1" goto generate
if not exist "%BUILD_DIR%\CMakeCache.txt" goto generate
echo [raven] removing "%BUILD_DIR%"
rmdir /S /Q "%BUILD_DIR%"

:generate
set "GENERATOR_ARGS="
if defined GENERATOR set GENERATOR_ARGS=-G "%GENERATOR%"
if defined ARCH set GENERATOR_ARGS=%GENERATOR_ARGS% -A %ARCH%

echo [raven] generating "%BUILD_DIR%"
cmake -S "%ROOT%" -B "%BUILD_DIR%" %GENERATOR_ARGS% -DRAVEN_BUILD_SHARED=%SHARED% -DRAVEN_BUILD_EXAMPLES=%EXAMPLES% -DRAVEN_BUILD_TESTS=%TESTS% -DRAVEN_BACKEND_VULKAN=%VULKAN% %EXTRA%
if errorlevel 1 goto failed

echo.
echo [raven] build tree generated in "%BUILD_DIR%".
echo         Build it with build.bat, or open the solution it contains.
if "%OPEN%"=="1" cmake --open "%BUILD_DIR%"
exit /b 0

:failed
echo.
echo [raven] generation FAILED.
if "%NO_PAUSE%"=="0" pause
exit /b 1

:usage
echo Usage: generate-cmake.bat [options] [-- extra cmake arguments]
echo.
echo   --build-dir DIR     build tree, "build" by default
echo   --generator NAME    CMake generator, the newest Visual Studio by default
echo   --arch NAME         x64, Win32 or ARM64, for the Visual Studio generators
echo   --shared            build raven.dll instead of a static library
echo   --no-examples       do not build the examples
echo   --no-tests          do not build the tests
echo   --no-vulkan         leave the Vulkan backend out
echo   --fresh             delete the build tree first
echo   --open              open the generated solution in Visual Studio
echo   --no-pause          do not wait for a key when the generation fails
exit /b 1
