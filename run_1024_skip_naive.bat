@echo off
chcp 65001 > nul
set "ROOT=%~dp0"
for %%I in ("%ROOT%.") do set "ROOT=%%~fI"

call "%ROOT%\setup_env.bat" %1
if errorlevel 1 (
    pause
    exit /b 1
)

set "EXE=%ROOT%\cmake-build-release\m_lab.exe"
if not exist "%EXE%" set "EXE=%ROOT%\cmake-build-release\Release\m_lab.exe"
if not exist "%EXE%" set "EXE=%ROOT%\build-release\m_lab.exe"
if not exist "%EXE%" set "EXE=%ROOT%\build-release\Release\m_lab.exe"
if not exist "%EXE%" set "EXE=%ROOT%\cmake-build-debug\m_lab.exe"
if not exist "%EXE%" set "EXE=%ROOT%\cmake-build-debug\Debug\m_lab.exe"

if not exist "%EXE%" (
    echo [ERROR] m_lab.exe не найден.
    echo Сначала запусти build_release.bat или собери проект в CLion.
    pause
    exit /b 1
)

echo.
echo === Запуск n=1024 без наивного варианта ===
"%EXE%" --n 1024 --threads %THREADS% --skip-naive
pause
