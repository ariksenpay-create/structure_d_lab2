cmake_minimum_required(VERSION 3.20)
project(m_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MKL_ROOT "C:/Program Files (x86)/Intel/oneAPI/mkl/latest")

add_executable(m_lab main.cpp)

target_include_directories(m_lab PRIVATE "${MKL_ROOT}/include")
target_link_libraries(m_lab PRIVATE "${MKL_ROOT}/lib/mkl_rt.lib")

if(MINGW)
    target_compile_options(m_lab PRIVATE -O3 -fopenmp)
    target_link_libraries(m_lab PRIVATE -fopenmp)
elseif(MSVC)
    find_package(OpenMP REQUIRED)
    target_compile_options(m_lab PRIVATE /O2 /utf-8)
    target_link_libraries(m_lab PRIVATE OpenMP::OpenMP_CXX)
endif()
