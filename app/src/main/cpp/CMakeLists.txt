
cmake_minimum_required(VERSION 3.22.1)

project(storm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
    storm
    SHARED
    main.cpp
)

add_library(
    native_app_glue
    STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)

target_include_directories(
    storm PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)

target_link_libraries(
    storm
    native_app_glue
    android
    log
    EGL
    GLESv3
)

target_compile_options(
    storm PRIVATE
    -O2
    -ffast-math
)
