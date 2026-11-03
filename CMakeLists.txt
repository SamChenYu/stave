cmake_minimum_required(VERSION 3.20)
project(stave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(STAVE_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})

# Our own code gets strict warnings; third-party code in external/ doesn't.
function(stave_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---- Dependencies (git submodules in external/)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/lunasvg/CMakeLists.txt)
    message(FATAL_ERROR "external/lunasvg is empty. Run: git submodule update --init")
endif()

# lunasvg: renders SVG into pixel buffers. Builds plutovg (its rasterizer, in C) from its own tree.
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/lunasvg EXCLUDE_FROM_ALL)

# ---- stave_core: everything except main(), shared by the program and the tests
add_library(stave_core STATIC
    src/encode.cpp
    src/keys.cpp
    src/svgimage.cpp
    src/terminal.cpp
    src/termprobe.cpp
    src/testpattern.cpp
    src/view.cpp)
target_include_directories(stave_core PUBLIC src)
target_link_libraries(stave_core PUBLIC lunasvg::lunasvg)
stave_warnings(stave_core)

# ---- stave
add_executable(stave src/main.cpp)
target_compile_definitions(stave PRIVATE STAVE_VERSION="${PROJECT_VERSION}")
target_link_libraries(stave PRIVATE stave_core)
stave_warnings(stave)

# ---- tests
if(STAVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
