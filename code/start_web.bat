@echo off
rem ============================================================
rem  start_web.bat  -  one click: build if needed, open browser, run server
rem  Stop: press Ctrl + C in this window
rem ============================================================
setlocal
cd /d "%~dp0"
set "PATH=D:\dev-tools\mingw64\bin;%PATH%"

if not exist server.exe (
    echo [web] server.exe not found, building now...
    call build_web.bat
    if not exist server.exe (
        echo [web] build failed, abort.
        pause
        exit /b 1
    )
)

echo.
echo ============================================================
echo   Warehouse Management System - Web Version
echo   URL    : http://localhost:8080
echo   Backend: C (Winsock HTTP server, no framework)
echo   Stop   : press Ctrl + C in this window
echo ============================================================
echo.

rem open the browser a couple of seconds later, in a child process
start "" /b cmd /c "timeout /t 2 /nobreak >nul & start http://localhost:8080"

server.exe 8080
endlocal
