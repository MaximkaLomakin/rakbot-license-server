@echo off
setlocal

set "SERVICE=RakLicense"
set "DISPLAY=RakBot License Server"
set "EXE=%~dp0raklicense.exe"
set "HOSTS=%SystemRoot%\System32\drivers\etc\hosts"
set "TMPH=%TEMP%\hosts.raklicense.tmp"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=80"

net session >nul 2>&1
if errorlevel 1 (
    echo Run this file as Administrator.
    pause
    exit /b 1
)

if not exist "%EXE%" (
    echo ERROR: raklicense.exe not found next to this script.
    pause
    exit /b 1
)

sc.exe query "%SERVICE%" >nul 2>&1
if not errorlevel 1 (
    echo Service "%SERVICE%" is already installed.
    echo Run uninstall-service.bat first.
    pause
    exit /b 1
)

if "%PORT%"=="80" (
    sc.exe create "%SERVICE%" binPath= "\"%EXE%\"" start= auto DisplayName= "%DISPLAY%"
) else (
    sc.exe create "%SERVICE%" binPath= "\"%EXE%\" --port %PORT%" start= auto DisplayName= "%DISPLAY%"
)
if errorlevel 1 goto :fail

sc.exe description "%SERVICE%" "Local RakBot license listener on 127.0.0.1:%PORT%" >nul
sc.exe failure "%SERVICE%" reset= 86400 actions= restart/5000/restart/5000/restart/5000 >nul
sc.exe start "%SERVICE%"
if errorlevel 1 goto :fail

rem sc start returns while the service is still START_PENDING; wait and verify.
ping 127.0.0.1 -n 3 >nul
sc.exe query "%SERVICE%" | findstr /c:"RUNNING" >nul
if errorlevel 1 goto :fail

rem --- redirect rakbot.ru to the local service ---
if not exist "%HOSTS%" (
    echo ERROR: hosts file not found: %HOSTS%
    goto :fail
)

if not exist "%HOSTS%.raklicense.bak" copy /y "%HOSTS%" "%HOSTS%.raklicense.bak" >nul

rem Drop any previous rakbot.ru lines (including an old manual redirect).
findstr /v /i /c:"rakbot.ru" "%HOSTS%" > "%TMPH%"
if errorlevel 2 (
    echo ERROR: failed to read the hosts file.
    goto :fail
)
rem findstr may leave the last line without CRLF; terminate it first.
>>"%TMPH%" echo.
>>"%TMPH%" echo # RakLicense
>>"%TMPH%" echo 127.0.0.1 rakbot.ru
copy /y "%TMPH%" "%HOSTS%" >nul
if errorlevel 1 (
    echo ERROR: failed to write the hosts file.
    del "%TMPH%" >nul 2>&1
    goto :fail
)
del "%TMPH%" >nul 2>&1
ipconfig /flushdns >nul 2>&1

echo.
echo Service "%SERVICE%" is running on 127.0.0.1:%PORT%.
echo Hosts: rakbot.ru -^> 127.0.0.1
echo Log: %~dp0raklicense.log
pause
exit /b 0

:fail
echo.
echo ERROR: failed to install or start the service.
echo Check: sc.exe query "%SERVICE%"
echo Log:   %~dp0raklicense.log
pause
exit /b 1
