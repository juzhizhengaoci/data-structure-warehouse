@echo off
rem ============================================================
rem  build_web.bat  -  build the Web version (server.exe)
rem  Compiles the web entry + shared modules. main.c is excluded
rem  (two main() functions would conflict).
rem  Links ws2_32: Windows Socket library.
rem ============================================================
setlocal
cd /d "%~dp0"
set "PATH=D:\dev-tools\mingw64\bin;%PATH%"

if exist server.exe del server.exe

echo [build] compiling server.exe ...
gcc -O2 -Wall -std=c99 -finput-charset=UTF-8 -fexec-charset=UTF-8 webserver.c list.c io.c file.c stock.c sort.c stat.c -o server.exe -lws2_32

if errorlevel 1 (
    echo.
    echo [build] FAILED - fix the errors above and try again.
    pause
    exit /b 1
)

echo.
echo [build] OK  -^>  server.exe
endlocal
