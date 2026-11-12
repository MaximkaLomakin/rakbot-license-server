@echo off
setlocal

set "SERVICE=RakLicense"
set "HOSTS=%SystemRoot%\System32\drivers\etc\hosts"
set "TMPH=%TEMP%\hosts.raklicense.tmp"

net session >nul 2>&1
if errorlevel 1 (
    echo Run this file as Administrator.
    pause
    exit /b 1
)

rem --- remove our hosts entry first ---
if exist "%HOSTS%" (
    findstr /v /x /i /c:"# RakLicense" /c:"127.0.0.1 rakbot.ru" "%HOSTS%" > "%TMPH%"
    if not errorlevel 2 (
        copy /y "%TMPH%" "%HOSTS%" >nul
        if errorlevel 1 (
            echo ERROR: failed to write the hosts file.
            del "%TMPH%" >nul 2>&1
            pause
            exit /b 1
        )
        del "%TMPH%" >nul 2>&1
        ipconfig /flushdns >nul 2>&1
        echo Hosts entry removed.
    )
)

sc.exe query "%SERVICE%" >nul 2>&1
if errorlevel 1 (
    echo Service "%SERVICE%" is not installed.
    pause
    exit /b 0
)

sc.exe stop "%SERVICE%" >nul 2>&1
rem ping works as a delay even when stdin is redirected (timeout does not).
ping 127.0.0.1 -n 4 >nul
sc.exe delete "%SERVICE%"
if errorlevel 1 (
    echo ERROR: failed to delete the service.
    pause
    exit /b 1
)

echo Service "%SERVICE%" removed.
pause
exit /b 0
