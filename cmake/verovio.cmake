# Verovio: music engraving (MusicXML, MEI, ABC in; SVG out), built from external/verovio as a
# static library. Its own CMake only builds a command-line tool or a C-wrapper library, so we
# list the sources here, leaving out the importers stave doesn't need.
#
# Licence: Verovio is LGPL-3.0. Linking it statically is fine for development builds; before
# distributing binaries, either switch to a shared library or ship what users need to relink.

set(VRV ${PROJECT_SOURCE_DIR}/external/verovio)
if(NOT EXISTS ${VRV}/include/vrv/toolkit.h)
    message(FATAL_ERROR "external/verovio is empty. Run: git submodule update --init")
endif()

file(GLOB VRV_SOURCES CONFIGURE_DEPENDS
    ${VRV}/src/*.cpp
    ${VRV}/src/midi/*.cpp
    ${VRV}/src/crc/*.cpp
    ${VRV}/libmei/dist/*.cpp
    ${VRV}/libmei/addons/*.cpp)
list(APPEND VRV_SOURCES ${VRV}/src/json/jsonxx.cc ${VRV}/src/pugi/pugixml.cpp)

add_library(verovio STATIC ${VRV_SOURCES})
# SYSTEM: warnings from Verovio's headers don't show up when building our code.
target_include_directories(verovio SYSTEM PUBLIC
    ${VRV}/include ${VRV}/include/vrv ${VRV}/include/crc ${VRV}/include/midi ${VRV}/include/hum
    ${VRV}/include/json ${VRV}/include/pugi ${VRV}/include/tuning-library ${VRV}/include/zip
    ${VRV}/libmei/dist ${VRV}/libmei/addons)
if(WIN32)
    target_include_directories(verovio SYSTEM PUBLIC ${VRV}/include/win32)
endif()
# These change what Verovio's headers declare, so everything including them must agree: PUBLIC.
target_compile_definitions(verovio PUBLIC
    NO_HUMDRUM_SUPPORT  # by far the largest importer; MusicXML doesn't need it
    NO_PAE_SUPPORT      # Plaine & Easie (incipit catalogues); upstream turns it off on MSVC anyway
    NO_DARMS_SUPPORT
    NO_RUNTIME
    RESOURCE_DIR="")    # stave always passes the data folder at run time
# Verovio reports its version as X.Y.Z plus a git commit from a generated header. We're on an
# exact release tag, so the suffix is empty. Generated in the build folder: the submodule stays clean.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/verovio-generated/git_commit.h "#define GIT_COMMIT \"\"\n")
target_include_directories(verovio PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/verovio-generated)

if(MSVC)
    # /w alone isn't enough: the standard library's headers raise the warning level inside
    # themselves, so conversion warnings from Verovio's use of them need turning off by number.
    target_compile_options(verovio PRIVATE /bigobj /utf-8 /MP /w /wd4244 /wd4267)
else()
    target_compile_options(verovio PRIVATE -w)
endif()

# Verovio loads its music fonts at run time, from a data folder. stave_copy_verovio_data(target)
# puts that folder next to the target's executable, as verovio-data/. Leipzig.ttf is the same
# music font as a real font file, for symbols inside text (such as the note in a tempo mark).
function(stave_copy_verovio_data target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${VRV}/data $<TARGET_FILE_DIR:${target}>/verovio-data
        COMMAND ${CMAKE_COMMAND} -E copy ${VRV}/fonts/Leipzig/Leipzig.ttf
                $<TARGET_FILE_DIR:${target}>/verovio-data/Leipzig.ttf
        VERBATIM)
endfunction()
