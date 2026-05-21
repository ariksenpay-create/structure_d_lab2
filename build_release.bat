@echo off
chcp 65001 > nul
setlocal EnableExtensions

rem Нормализуем путь к папке проекта без завершающего обратного слэша.
set "ROOT=%~dp0"
for %%I in ("%ROOT%.") do set "ROOT=%%~fI"

call "%ROOT%\setup_env.bat" %1
if errorlevel 1 (
    pause
    exit /b 1
)

set "BUILD_DIR=%ROOT%\cmake-build-release"

rem === Ищем CMake ===
set "CMAKE_EXE="
if exist "C:\Program Files\JetBrains\CLion 2025.2.1\bin\cmake\win\x64\bin\cmake.exe" (
    set "CMAKE_EXE=C:\Program Files\JetBrains\CLion 2025.2.1\bin\cmake\win\x64\bin\cmake.exe"
)
if not defined CMAKE_EXE (
    for /f "delims=" %%F in ('where cmake 2^>nul') do (
        if not defined CMAKE_EXE set "CMAKE_EXE=%%F"
    )
)
if not defined CMAKE_EXE (
    for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do (
        if exist "%%~fD\bin\cmake\win\x64\bin\cmake.exe" (
            set "CMAKE_EXE=%%~fD\bin\cmake\win\x64\bin\cmake.exe"
            goto :cmake_found
        )
    )
)
:cmake_found
if not defined CMAKE_EXE (
    echo [ERROR] cmake.exe не найден.
    echo Установи CMake или укажи путь к нему в build_release.bat.
    pause
    exit /b 1
)

rem === Ищем MinGW g++ из CLion или из PATH ===
set "CXX_EXE="
if exist "C:\Program Files\JetBrains\CLion 2025.2.1\bin\mingw\bin\g++.exe" (
    set "CXX_EXE=C:\Program Files\JetBrains\CLion 2025.2.1\bin\mingw\bin\g++.exe"
)
if not defined CXX_EXE (
    for /f "delims=" %%F in ('where g++ 2^>nul') do (
        if not defined CXX_EXE set "CXX_EXE=%%F"
    )
)
if not defined CXX_EXE (
    for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do (
        if exist "%%~fD\bin\mingw\bin\g++.exe" (
            set "CXX_EXE=%%~fD\bin\mingw\bin\g++.exe"
            goto :cxx_found
        )
    )
)
:cxx_found
if not defined CXX_EXE (
    echo [ERROR] g++.exe не найден.
    echo В CLion проверь Toolchains или установи MinGW.
    pause
    exit /b 1
)
for %%I in ("%CXX_EXE%") do set "MINGW_BIN=%%~dpI"
set "PATH=%MINGW_BIN%;%PATH%"

rem === Ищем Ninja. Если Ninja нет, используем MinGW Makefiles ===
set "NINJA_EXE="
if exist "C:\Program Files\JetBrains\CLion 2025.2.1\bin\ninja\win\x64\ninja.exe" (
    set "NINJA_EXE=C:\Program Files\JetBrains\CLion 2025.2.1\bin\ninja\win\x64\ninja.exe"
)
if not defined NINJA_EXE (
    for /f "delims=" %%F in ('where ninja 2^>nul') do (
        if not defined NINJA_EXE set "NINJA_EXE=%%F"
    )
)
if not defined NINJA_EXE (
    for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do (
        if exist "%%~fD\bin\ninja\win\x64\ninja.exe" (
            set "NINJA_EXE=%%~fD\bin\ninja\win\x64\ninja.exe"
            goto :ninja_found
        )
    )
)
:ninja_found

set "MAKE_EXE="
if not defined NINJA_EXE (
    for /f "delims=" %%F in ('where mingw32-make 2^>nul') do (
        if not defined MAKE_EXE set "MAKE_EXE=%%F"
    )
    if not defined MAKE_EXE if exist "%MINGW_BIN%mingw32-make.exe" set "MAKE_EXE=%MINGW_BIN%mingw32-make.exe"
)

echo.
echo === Configure Release ===
echo Project:  "%ROOT%"
echo Build:    "%BUILD_DIR%"
echo MKL:      "%MKL_ROOT%"
echo CMake:    "%CMAKE_EXE%"
echo Compiler: "%CXX_EXE%"
if defined NINJA_EXE echo Generator: Ninja, "%NINJA_EXE%"
if not defined NINJA_EXE if defined MAKE_EXE echo Generator: MinGW Makefiles, "%MAKE_EXE%"

rem Удаляем старую конфигурацию, чтобы не конфликтовали генераторы NMake/Ninja/MinGW.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [INFO] Удаляю старую CMake-конфигурацию Release...
    rmdir /s /q "%BUILD_DIR%"
)

if defined NINJA_EXE (
    "%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "Ninja" "-DCMAKE_MAKE_PROGRAM:FILEPATH=%NINJA_EXE%" "-DCMAKE_CXX_COMPILER:FILEPATH=%CXX_EXE%" "-DCMAKE_BUILD_TYPE:STRING=Release" "-DMKL_ROOT:PATH=%MKL_ROOT%"
) else (
    if not defined MAKE_EXE (
        echo [ERROR] Не найден ни ninja.exe, ни mingw32-make.exe.
        echo Проверь установку CLion MinGW/Ninja.
        pause
        exit /b 1
    )
    "%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" "-DCMAKE_MAKE_PROGRAM:FILEPATH=%MAKE_EXE%" "-DCMAKE_CXX_COMPILER:FILEPATH=%CXX_EXE%" "-DCMAKE_BUILD_TYPE:STRING=Release" "-DMKL_ROOT:PATH=%MKL_ROOT%"
)

if errorlevel 1 (
    echo [ERROR] Ошибка конфигурации CMake.
    pause
    exit /b 1
)

echo.
echo === Build Release ===
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release

if errorlevel 1 (
    echo [ERROR] Ошибка сборки.
    pause
    exit /b 1
)

echo.
echo [OK] Сборка завершена.
echo EXE: "%BUILD_DIR%\m_lab.exe"
pause
exit /b 0
