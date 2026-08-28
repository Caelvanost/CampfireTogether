@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "PROJECT=%~dp0.."
for %%I in ("%PROJECT%") do set "PROJECT=%%~fI"

for /f "usebackq delims=" %%V in ("%PROJECT%\VERSION") do set "VERSION=%%V"

if not defined VCPKG_ROOT set "VCPKG_ROOT=C:\dev\vcpkg"
if not defined SKYRIM_PATH set "SKYRIM_PATH=C:\Games\Steam\steamapps\common\Skyrim Special Edition"

set "OUT=%PROJECT%\build\out"
set "PACKAGE=%PROJECT%\build\package"
set "ZIP=%PROJECT%\build\CampfireTogether-v%VERSION%.zip"
set "COMPILER=%SKYRIM_PATH%\Papyrus Compiler\PapyrusCompiler.exe"
set "FLAGS=%SKYRIM_PATH%\Data\Source\Scripts\TESV_Papyrus_Flags.flg"
set "VANILLA_SOURCE=%SKYRIM_PATH%\Data\Source\Scripts"
set "CFT_SOURCE=%PROJECT%\Scripts\Source"
set "OVERRIDE_SOURCE=%PROJECT%\Scripts\Overrides"
set "COMPILE_STUBS=%PROJECT%\Scripts\CompileStubs"
set "PAPYRUS_IMPORTS=%CFT_SOURCE%;%COMPILE_STUBS%;%VANILLA_SOURCE%"
set "SPRIGGIT_SOURCE=%PROJECT%\plugin\CampfireTogether"

echo.
echo # Campfire Together v%VERSION% - Release Build
echo.
echo Using vcpkg: %VCPKG_ROOT%
echo Skyrim: %SKYRIM_PATH%
echo.

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: vcpkg toolchain not found.
    exit /b 1
)

if exist "%OUT%" rmdir /s /q "%OUT%"
if exist "%PACKAGE%" rmdir /s /q "%PACKAGE%"
if exist "%ZIP%" del /q "%ZIP%"

mkdir "%OUT%" >nul 2>&1
mkdir "%PACKAGE%" >nul 2>&1

echo [1/5] Configuring...
cmake -S "%PROJECT%" -B "%OUT%" -G "Visual Studio 18 2026" -A x64 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 exit /b 1

echo.
echo [2/5] Building DLL...
cmake --build "%OUT%" --config Release
if errorlevel 1 exit /b 1

if not exist "%PACKAGE%\SKSE\Plugins\CampfireTogether.dll" (
    if exist "%OUT%\package\SKSE\Plugins\CampfireTogether.dll" (
        mkdir "%PACKAGE%\SKSE\Plugins" >nul 2>&1
        copy /y "%OUT%\package\SKSE\Plugins\CampfireTogether.dll" "%PACKAGE%\SKSE\Plugins\CampfireTogether.dll" >nul
    )
)

if not exist "%PACKAGE%\SKSE\Plugins\CampfireTogether.dll" (
    echo ERROR: CampfireTogether.dll was not produced.
    exit /b 1
)

echo.
echo [3/5] Compiling Papyrus scripts...
if not exist "%COMPILER%" (
    echo ERROR: PapyrusCompiler.exe not found: %COMPILER%
    exit /b 1
)
if not exist "%FLAGS%" (
    echo ERROR: Papyrus flags not found: %FLAGS%
    exit /b 1
)
mkdir "%PACKAGE%\Scripts" >nul 2>&1

"%COMPILER%" "%CFT_SOURCE%\CampfireTogetherNative.psc" -f="%FLAGS%" -i="%PAPYRUS_IMPORTS%" -o="%PACKAGE%\Scripts"
if errorlevel 1 exit /b 1
"%COMPILER%" "%CFT_SOURCE%\CampfireTogetherBridge.psc" -f="%FLAGS%" -i="%PAPYRUS_IMPORTS%" -o="%PACKAGE%\Scripts"
if errorlevel 1 exit /b 1
"%COMPILER%" "%OVERRIDE_SOURCE%\CampConjureObjectEffect.psc" -f="%FLAGS%" -i="%PAPYRUS_IMPORTS%" -o="%PACKAGE%\Scripts"
if errorlevel 1 exit /b 1
"%COMPILER%" "%OVERRIDE_SOURCE%\_Camp_CampTentNPCBedrollScript.psc" -f="%FLAGS%" -i="%PAPYRUS_IMPORTS%" -o="%PACKAGE%\Scripts"
if errorlevel 1 exit /b 1

if not exist "%PACKAGE%\Scripts\CampfireTogetherNative.pex" (
    echo ERROR: CampfireTogetherNative.pex was not produced.
    exit /b 1
)
if not exist "%PACKAGE%\Scripts\CampfireTogetherBridge.pex" (
    echo ERROR: CampfireTogetherBridge.pex was not produced.
    exit /b 1
)
if not exist "%PACKAGE%\Scripts\CampConjureObjectEffect.pex" (
    echo ERROR: CampConjureObjectEffect.pex was not produced.
    exit /b 1
)
if not exist "%PACKAGE%\Scripts\_Camp_CampTentNPCBedrollScript.pex" (
    echo ERROR: _Camp_CampTentNPCBedrollScript.pex was not produced.
    exit /b 1
)

echo.
echo [4/5] Building ESPFE with Spriggit...
where dotnet >nul 2>&1
if errorlevel 1 (
    echo ERROR: dotnet SDK was not found in PATH.
    echo Spriggit 0.40.1 requires the .NET 9 SDK or newer.
    exit /b 1
)
if not exist "%SPRIGGIT_SOURCE%\spriggit-meta.json" (
    echo ERROR: Spriggit metadata not found: %SPRIGGIT_SOURCE%\spriggit-meta.json
    exit /b 1
)

pushd "%PROJECT%"
dotnet tool restore
if errorlevel 1 (
    popd
    exit /b 1
)

dotnet tool run spriggit deserialize --InputPath "%SPRIGGIT_SOURCE%" --OutputPath "%PACKAGE%\CampfireTogether.esp"
if errorlevel 1 (
    popd
    exit /b 1
)
popd

if not exist "%PACKAGE%\CampfireTogether.esp" (
    echo ERROR: Spriggit did not produce CampfireTogether.esp.
    exit /b 1
)

echo.
echo [5/5] Packaging...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%PACKAGE%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 exit /b 1

echo.
echo Build complete:
echo   DLL: %PACKAGE%\SKSE\Plugins\CampfireTogether.dll
echo   PEX: %PACKAGE%\Scripts\CampfireTogetherNative.pex
echo   PEX: %PACKAGE%\Scripts\CampfireTogetherBridge.pex
echo   PEX: %PACKAGE%\Scripts\CampConjureObjectEffect.pex
echo   PEX: %PACKAGE%\Scripts\_Camp_CampTentNPCBedrollScript.pex
echo   ESP: %PACKAGE%\CampfireTogether.esp
echo   ZIP: %ZIP%
echo.
endlocal
