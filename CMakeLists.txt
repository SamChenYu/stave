cmake_minimum_required(VERSION 3.20)
project(stave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(stave src/main.cpp)
target_compile_definitions(stave PRIVATE STAVE_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(stave PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(stave PRIVATE -Wall -Wextra -Wpedantic)
endif()
