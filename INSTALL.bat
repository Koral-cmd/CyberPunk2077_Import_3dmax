@echo off
REM ================================================================
REM  INSTALL.bat - puts the built plugin into
REM    %APPDATA%\Autodesk\ApplicationPlugins\CP2077Import
REM  (per user, no admin rights). 3ds Max loads it on the next start:
REM  main menu "CP2077".  Close 3ds Max first.
REM ================================================================
setlocal
cd /d "%~dp0"
set "DEST=%APPDATA%\Autodesk\ApplicationPlugins\CP2077Import"
set "FOUND=0"
for %%Y in (2022 2023 2024 2025 2026 2027) do if exist "output\%%Y\CP2077Import.dlu" set "FOUND=1"
if "%FOUND%"=="0" (
    echo Nothing built yet - run BUILD.bat first.
    goto end
)
if exist "%DEST%\Contents" rmdir /s /q "%DEST%\Contents"
if exist "%DEST%\Contents" (
    echo %DEST% is in use - close 3ds Max and run again.
    goto end
)
mkdir "%DEST%\Contents\MacroScripts" "%DEST%\Contents\Post-Start-Up_Scripts" "%DEST%\Contents\Pre-Start-Up_Scripts" >nul 2>&1
copy /Y "package\CP2077Import\PackageContents.xml" "%DEST%\" >nul
for %%Y in (2022 2023 2024 2025 2026 2027) do if exist "output\%%Y\CP2077Import.dlu" (
    mkdir "%DEST%\Contents\%%Y" >nul 2>&1
    copy /Y "output\%%Y\CP2077Import.dlu" "%DEST%\Contents\%%Y\" >nul
    echo   3ds Max %%Y
)
copy /Y "scripts\CP2077Import.mcr" "%DEST%\Contents\MacroScripts\" >nul
copy /Y "scripts\CP2077Menu_2022_2024.ms" "%DEST%\Contents\Post-Start-Up_Scripts\" >nul
copy /Y "scripts\CP2077Menu_2025.ms" "%DEST%\Contents\Pre-Start-Up_Scripts\" >nul
if exist "output\cp77probe.exe" copy /Y "output\cp77probe.exe" "%DEST%\Contents\" >nul
echo Installed to %DEST%
:end
if /i not "%~1"=="nopause" pause
