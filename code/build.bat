@echo off
rem ============================================================
rem  build.bat  -  compile all .c files in this folder into app.exe
rem  Toolchain : MinGW-w64 (gcc)  ->  D:\dev-tools\mingw64
rem  Usage     : double-click, or type build.bat, or Ctrl+Shift+B in VS Code
rem
rem  NOTE: we deliberately cd into this folder first and pass only
rem  ASCII (relative) paths to gcc. MinGW's linker cannot handle
rem  non-ASCII (Chinese) paths in -o arguments.
rem ============================================================
setlocal
cd /d "%~dp0"
set "PATH=D:\dev-tools\mingw64\bin;%PATH%"

if exist app.exe del app.exe

echo [build] compiling console version (app.exe) ...
rem NOTE: webserver.c belongs to the Web version and has its own main().
rem It must be EXCLUDED here, otherwise the linker reports duplicate main.
gcc -g -Wall -std=c99 -finput-charset=UTF-8 -fexec-charset=GBK main.c list.c io.c file.c stock.c sort.c stat.c -o app.exe

if errorlevel 1 (
    echo.
    echo [build] FAILED - fix the errors above and try again.
    pause
    exit /b 1
)

echo.
echo [build] OK  -^>  app.exe
endlocal
