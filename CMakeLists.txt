cmake_minimum_required(VERSION 3.20)

project(m_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Путь к Intel oneMKL. Можно переопределить при конфигурации:
# cmake -S . -B cmake-build-release -DMKL_ROOT="C:/Program Files (x86)/Intel/oneAPI/mkl/latest"
set(MKL_ROOT "C:/Program Files (x86)/Intel/oneAPI/mkl/latest" CACHE PATH "Intel oneMKL root directory")

set(MKL_INCLUDE_DIR "${MKL_ROOT}/include")
set(MKL_LIB_DIRS
    "${MKL_ROOT}/lib"
    "${MKL_ROOT}/lib/intel64"
)

find_path(MKL_INCLUDE_PATH
    NAMES mkl.h
    PATHS "${MKL_INCLUDE_DIR}"
    NO_DEFAULT_PATH
)

find_library(MKL_RT_LIBRARY
    NAMES mkl_rt
    PATHS ${MKL_LIB_DIRS}
    NO_DEFAULT_PATH
)

if(NOT MKL_INCLUDE_PATH)
    message(FATAL_ERROR "Не найден mkl.h. Проверь MKL_ROOT=${MKL_ROOT}")
endif()

if(NOT MKL_RT_LIBRARY)
    message(FATAL_ERROR "Не найден mkl_rt.lib. Проверь MKL_ROOT=${MKL_ROOT}")
endif()

add_executable(m_lab
    src/main.cpp
)

target_include_directories(m_lab PRIVATE
    "${MKL_INCLUDE_PATH}"
)

# В проекте внешняя библиотека только Intel MKL.
# OpenMP не используется: 3-й вариант распараллелен через std::thread.
target_link_libraries(m_lab PRIVATE
    "${MKL_RT_LIBRARY}"
)

target_compile_definitions(m_lab PRIVATE
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(m_lab PRIVATE
        /O2
        /Ob3
        /Oi
        /fp:fast
        /utf-8
        /arch:AVX2
    )
elseif(MINGW)
    target_compile_options(m_lab PRIVATE
        -O3
        -march=native
        -ffast-math
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )

    # Чтобы не требовались DLL libgcc_s_seh-1.dll и libstdc++-6.dll.
    # winpthread пытаемся подключить статически, потому что std::thread в MinGW
    # часто использует этот runtime.
    target_link_options(m_lab PRIVATE
        -static-libgcc
        -static-libstdc++
    )

    target_link_libraries(m_lab PRIVATE
        -Wl,-Bstatic
        winpthread
        -Wl,-Bdynamic
    )
else()
    target_compile_options(m_lab PRIVATE
        -O3
        -march=native
        -ffast-math
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# Для запуска из CLion без ручной настройки PATH копируем DLL oneMKL рядом с exe.
# В ZIP эти DLL не входят; они копируются с твоего установленного Intel oneAPI.
if(WIN32)
    set(_MKL_DLL_DIRS
        "${MKL_ROOT}/redist/intel64"
        "${MKL_ROOT}/bin/intel64"
    )

    set(_MKL_RUNTIME_DLLS "")
    foreach(_dll_dir IN LISTS _MKL_DLL_DIRS)
        if(EXISTS "${_dll_dir}")
            file(GLOB _dlls_in_dir CONFIGURE_DEPENDS
                "${_dll_dir}/mkl*.dll"
                "${_dll_dir}/libiomp5md.dll"
            )
            list(APPEND _MKL_RUNTIME_DLLS ${_dlls_in_dir})
        endif()
    endforeach()

    if(_MKL_RUNTIME_DLLS)
        list(REMOVE_DUPLICATES _MKL_RUNTIME_DLLS)
        foreach(_dll IN LISTS _MKL_RUNTIME_DLLS)
            add_custom_command(TARGET m_lab POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}"
                "$<TARGET_FILE_DIR:m_lab>"
            )
        endforeach()
    else()
        message(WARNING "DLL oneMKL не найдены в ${MKL_ROOT}/redist/intel64 или ${MKL_ROOT}/bin/intel64. Для запуска используй setup_env.bat.")
    endif()

    # Если используется MinGW, std::thread может требовать libwinpthread-1.dll.
    # Это runtime компилятора MinGW, не библиотека алгоритма и не замена MKL.
    # Копируем только эту DLL рядом с exe, чтобы запуск работал из PowerShell/CLion без ручного PATH.
    if(MINGW)
        get_filename_component(_MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(_WINPTHREAD_DLL "${_MINGW_BIN_DIR}/libwinpthread-1.dll")
        if(EXISTS "${_WINPTHREAD_DLL}")
            add_custom_command(TARGET m_lab POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_WINPTHREAD_DLL}"
                "$<TARGET_FILE_DIR:m_lab>"
            )
        else()
            message(WARNING "libwinpthread-1.dll не найден рядом с компилятором: ${_WINPTHREAD_DLL}. Если запуск упадёт, добавь папку MinGW/bin в PATH.")
        endif()
    endif()
endif()
