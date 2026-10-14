@echo off
rem ============================================================
rem  run.bat  -  compile (if needed) and run app.exe
rem ============================================================
setlocal
set "PATH=D:\dev-tools\mingw64\bin;%PATH%"
cd /d "%~dp0"

if not exist "app.exe" call "%~dp0build.bat"
if not exist "app.exe" goto :end

echo.
echo [run] starting app.exe ...
echo ------------------------------------------------------------
app.exe
echo ------------------------------------------------------------
echo [run] program exited.

:end
echo.
pause
endlocal
