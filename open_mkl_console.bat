@echo off
chcp 65001 > nul
set "ROOT=%~dp0"
for %%I in ("%ROOT%.") do set "ROOT=%%~fI"

call "%ROOT%\setup_env.bat" %1
if errorlevel 1 (
    pause
    exit /b 1
)

echo.
echo [OK] Окружение настроено. Эта консоль готова для cmake/build/run.
echo Папка проекта: %ROOT%
echo.
cmd /k
