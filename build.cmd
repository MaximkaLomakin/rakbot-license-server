@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio 2022 with C++ tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
    echo ERROR: Visual Studio 2022 with C++ tools not found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 (
    echo ERROR: failed to initialize the x86 build environment.
    exit /b 1
)
cd /d "%~dp0"

echo Building raklicense.exe ...
cl /nologo /O2 /MT /W3 src\raklicense.c /link ws2_32.lib advapi32.lib /out:raklicense.exe
if errorlevel 1 exit /b 1

echo.
echo Built: raklicense.exe
