@echo off
chcp 65001 > nul

rem === Intel oneAPI / MKL ===
set "ONEAPI_ROOT=C:\Program Files (x86)\Intel\oneAPI"
set "MKL_ROOT=%ONEAPI_ROOT%\mkl\latest"
set "MKLROOT=%MKL_ROOT%"

if not exist "%MKL_ROOT%\include\mkl.h" (
    echo [ERROR] Не найден mkl.h: "%MKL_ROOT%\include\mkl.h"
    echo Проверь путь MKL_ROOT в setup_env.bat.
    exit /b 1
)

rem Подключаем стандартное окружение oneAPI, если setvars.bat есть.
if exist "%ONEAPI_ROOT%\setvars.bat" (
    call "%ONEAPI_ROOT%\setvars.bat" > nul
)

rem Ручная настройка путей MKL. Это нужно для mkl_rt.dll и связанных DLL.
set "PATH=%MKL_ROOT%\redist\intel64;%MKL_ROOT%\bin\intel64;%PATH%"
set "INCLUDE=%MKL_ROOT%\include;%INCLUDE%"
set "LIB=%MKL_ROOT%\lib;%LIB%"

rem === MinGW runtime из CLion ===
rem libwinpthread-1.dll относится к MinGW/std::thread, а не к MKL.
rem Ищем папку MinGW и добавляем её в PATH для запуска из bat-файлов.
set "MINGW_BIN="
if exist "C:\Program Files\JetBrains\CLion 2025.2.1\bin\mingw\bin\libwinpthread-1.dll" (
    set "MINGW_BIN=C:\Program Files\JetBrains\CLion 2025.2.1\bin\mingw\bin"
)
if not defined MINGW_BIN (
    for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do (
        if not defined MINGW_BIN if exist "%%~fD\bin\mingw\bin\libwinpthread-1.dll" (
            set "MINGW_BIN=%%~fD\bin\mingw\bin"
        )
    )
)
if not defined MINGW_BIN (
    for /f "delims=" %%F in ('where libwinpthread-1.dll 2^>nul') do (
        if not defined MINGW_BIN for %%I in ("%%F") do set "MINGW_BIN=%%~dpI"
    )
)
if defined MINGW_BIN (
    set "PATH=%MINGW_BIN%;%PATH%"
)

rem Число потоков: можно передать первым аргументом, например setup_env.bat 8
if "%~1"=="" (
    if not defined THREADS set "THREADS=8"
) else (
    set "THREADS=%~1"
)

set "MKL_NUM_THREADS=%THREADS%"
set "MKL_DYNAMIC=FALSE"

echo [OK] UTF-8 включен: chcp 65001
echo [OK] MKL_ROOT=%MKL_ROOT%
echo [OK] PATH дополнен папками oneMKL runtime
if defined MINGW_BIN (
    echo [OK] PATH дополнен папкой MinGW runtime: %MINGW_BIN%
) else (
    echo [WARN] libwinpthread-1.dll не найден в CLion MinGW. Если программа не запустится, добавь MinGW\bin в PATH.
)
echo [OK] THREADS=%THREADS%
exit /b 0
