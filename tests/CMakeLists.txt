if(NOT EXISTS ${PROJECT_SOURCE_DIR}/external/doctest/CMakeLists.txt)
    message(FATAL_ERROR "external/doctest is empty. Run: git submodule update --init "
                        "(or configure with -DSTAVE_BUILD_TESTS=OFF)")
endif()

set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/external/doctest ${CMAKE_BINARY_DIR}/external/doctest EXCLUDE_FROM_ALL)
include(${PROJECT_SOURCE_DIR}/external/doctest/scripts/cmake/doctest.cmake)

add_executable(stave_tests
    support/golden.cpp
    support/image.cpp
    support/sixel_decoder.cpp
    golden_test.cpp
    keys_test.cpp
    pager_test.cpp
    score_test.cpp
    sixel_test.cpp
    svg_test.cpp
    termprobe_test.cpp
    view_test.cpp)
target_include_directories(stave_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# plutovg (built as part of lunasvg) reads and writes the PNG files.
target_link_libraries(stave_tests PRIVATE stave_core plutovg::plutovg doctest::doctest_with_main)
target_compile_definitions(stave_tests PRIVATE
    STAVE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    STAVE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    STAVE_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/output"
    _CRT_SECURE_NO_WARNINGS)  # allow plain std::getenv on MSVC
stave_warnings(stave_tests)
stave_copy_verovio_data(stave_tests)

# Registers every TEST_CASE with CTest under its own name.
doctest_discover_tests(stave_tests)
