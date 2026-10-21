@echo off
REM ================================================================
REM  BUILD.bat - builds CP2077Import.dlu for every installed 3ds Max
REM  SDK (2022 and newer) and cp77probe.exe, then installs the package.
REM    BUILD.bat          all SDKs found
REM    BUILD.bat 2025     only this year
REM  Needs Visual Studio 2022 or 2026 - Community or the free "Build Tools" -
REM  with the workload "Desktop development with C++".
REM  The SDK is expected in  C:\Program Files\Autodesk\3ds Max YEAR SDK\maxsdk
REM  (or set MAXSDK_YEAR, e.g.  set MAXSDK_2025=D:\sdk\maxsdk).
REM  Log: BUILD.log
REM ================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"
set "LOG=%~dp0BUILD.log"
set "OUT=%~dp0output"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE="
set "GEN="
set "VSVER="
set "VSPATH="
set "BUILT=0"
set "ONLY=%~1"
echo BUILD.bat %DATE% %TIME%> "%LOG%"

REM ---- Visual Studio with the C++ tools (Community, Professional or Build Tools) ----
if not exist "%VSWHERE%" goto no_vs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do set "VSVER=%%i"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSVER goto no_vs
if "%VSVER:~0,3%"=="18." set "GEN=Visual Studio 18 2026"
if "%VSVER:~0,3%"=="17." set "GEN=Visual Studio 17 2022"
if not defined GEN (
    echo Visual Studio %VSVER% found - it is too old. Install Visual Studio 2022 or 2026 with C++.
    goto end
)

REM ---- CMake: the copy inside that Visual Studio knows its own generator; else the one in PATH ----
if exist "%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE (
    where cmake >nul 2>&1 && set "CMAKE=cmake"
)
if not defined CMAKE (
    echo No CMake found. In the Visual Studio Installer, add "C++ CMake tools for Windows"
    echo ^(part of "Desktop development with C++"^), or install CMake from cmake.org.
    goto end
)
echo Visual Studio %VSVER%  ^(%VSPATH%^)
echo CMake: %CMAKE%
echo Generator: %GEN%
>>"%LOG%" echo VS %VSVER% at %VSPATH%, CMake %CMAKE%, generator %GEN%
set "CONF=-G "%GEN%" -A x64 -DCMAKE_GENERATOR_INSTANCE="%VSPATH%""

echo [1/3] cp77probe.exe ...
call :fresh build_tools
"%CMAKE%" -S . -B build_tools %CONF% -DCP77_BUILD_TOOLS=ON >> "%LOG%" 2>&1
"%CMAKE%" --build build_tools --config Release --target cp77probe >> "%LOG%" 2>&1
mkdir "%OUT%" >nul 2>&1
if exist "build_tools\Release\cp77probe.exe" copy /Y "build_tools\Release\cp77probe.exe" "%OUT%\cp77probe.exe" >nul
if exist "%OUT%\cp77probe.exe" (echo       ok) else (
    echo       failed - error lines:
    findstr /C:"error C" /C:"error LNK" /C:"error MSB" /C:"CMake Error" "%LOG%"
)

echo [2/3] Plugin ...
if not "%ONLY%"=="" (call :year %ONLY%) else (for %%Y in (2022 2023 2024 2025 2026 2027) do call :year %%Y)
if "%BUILT%"=="0" (
    echo No plugin built. Either no 3ds Max SDK was found - install the SDK of your Max
    echo version ^(Autodesk Account -^> Downloads^) into "C:\Program Files\Autodesk\3ds Max YEAR SDK" -
    echo or the build failed ^(error lines above, full log: BUILD.log^).
    goto end
)

echo [3/3] Install ...
call "%~dp0INSTALL.bat" nopause
goto end

REM A build folder configured with another generator (or a failed first try) makes CMake
REM refuse to configure again: drop its cache, keep the compiled objects.
:fresh
if exist "%~1\CMakeCache.txt" del /q "%~1\CMakeCache.txt"
if exist "%~1\CMakeFiles" rmdir /s /q "%~1\CMakeFiles"
goto :eof

:year
set "Y=%~1"
set "SDK=C:\Program Files\Autodesk\3ds Max %Y% SDK\maxsdk"
if defined MAXSDK_%Y% call set "SDK=%%MAXSDK_%Y%%%"
if not exist "%SDK%\include\max.h" goto :eof
echo       3ds Max %Y% ...
call :fresh "build_%Y%"
"%CMAKE%" -S . -B "build_%Y%" %CONF% -DCP77_BUILD_PLUGIN=ON -DCP77_BUILD_TOOLS=OFF -D3DSMAX_SDK_DIR="%SDK%" -DMAX_VERSION=%Y% >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
"%CMAKE%" --build "build_%Y%" --config Release >> "%LOG%" 2>&1
if errorlevel 1 goto :fail
if not exist "build_%Y%\bin\CP2077Import.dlu" goto :fail
mkdir "%OUT%\%Y%" >nul 2>&1
copy /Y "build_%Y%\bin\CP2077Import.dlu" "%OUT%\%Y%\CP2077Import.dlu" >nul
echo             ok -^> output\%Y%\CP2077Import.dlu
set /a BUILT+=1
goto :eof
:fail
if exist "%OUT%\%Y%\CP2077Import.dlu" del /q "%OUT%\%Y%\CP2077Import.dlu"
echo             FAILED - error lines:
findstr /C:"error C" /C:"error LNK" /C:"error MSB" /C:"CMake Error" "%LOG%"
goto :eof

:no_vs
echo.
echo Visual Studio with C++ was not found.
echo Install ONE of these (free), with the workload "Desktop development with C++":
echo   - Visual Studio 2022 Community
echo   - Build Tools for Visual Studio 2022 (no IDE, smaller)
echo   https://visualstudio.microsoft.com/downloads/
echo If Visual Studio is already installed: open "Visual Studio Installer" -^> Modify -^>
echo tick "Desktop development with C++" -^> Modify. Then run BUILD.bat again.
>>"%LOG%" echo no Visual Studio with VC tools found (vswhere: %VSWHERE%)

:end
echo.
echo Log: %LOG%
pause
