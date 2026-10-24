cmake_minimum_required(VERSION 3.16)
project(RDSM LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project version (shown in the About dialog; see specs/RDSM spec.md).
set(RDSM_VERSION "1.0")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(SOURCES
    src/main.cpp
    src/app.cpp
    src/shares.cpp
    src/editdialog.cpp
    src/settingsdialog.cpp
    src/aboutdialog.cpp
    src/resources/resources.rc
)

set(HEADERS
    src/app.h
    src/shares.h
    src/editdialog.h
    src/settingsdialog.h
    src/aboutdialog.h
    src/gdiplus_prelude.h
    src/resources/resources.h
    third_party/nlohmann/json.hpp
)

add_executable(RDSM WIN32 ${SOURCES} ${HEADERS})

# build_info.h (version + build timestamp) is regenerated on every build so the
# timestamp reflects the moment the build command was executed.
add_custom_target(BuildInfo ALL
    COMMAND ${CMAKE_COMMAND}
            -DRDSM_VERSION=${RDSM_VERSION}
            -DBINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cmake
    COMMENT "Generating build_info.h (version ${RDSM_VERSION} + build timestamp)"
    VERBATIM
)
add_dependencies(RDSM BuildInfo)

target_include_directories(RDSM PRIVATE
    src
    third_party
    "${CMAKE_BINARY_DIR}/generated"
)

target_compile_definitions(RDSM PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(RDSM PRIVATE
    comctl32
    gdiplus
    gdi32
    user32
    shell32
    ole32
    advapi32
    kernel32
)

if(MSVC)
    target_compile_options(RDSM PRIVATE /W4 /permissive- /utf-8)
    # The manifest is embedded via resources.rc; disable the linker's
    # auto-generated manifest to avoid a duplicate MANIFEST resource (CVT1100).
    target_link_options(RDSM PRIVATE /MANIFEST:NO)
endif()

# Put the exe in a predictable location
set_target_properties(RDSM PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------------------------
# Unit tests (CTest). See docs/TESTING.md.
#
# Only the non-GUI logic (src/shares.cpp) is unit tested; the window/dialog
# code needs a desktop and is verified by running the exe. Disable with
# -DBUILD_TESTING=OFF.
# ---------------------------------------------------------------------------
enable_testing()
option(BUILD_TESTING "Build the RDSM unit tests" ON)

if(BUILD_TESTING)
    add_executable(RDSMTests
        tests/main.cpp
        tests/test_format.cpp
        tests/test_query.cpp
        tests/test_config.cpp
        tests/test_validate.cpp
        src/shares.cpp
    )

    target_include_directories(RDSMTests PRIVATE
        src
        third_party
        tests
    )

    target_compile_definitions(RDSMTests PRIVATE
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    target_link_libraries(RDSMTests PRIVATE kernel32)

    if(MSVC)
        target_compile_options(RDSMTests PRIVATE /W4 /permissive- /utf-8)
    endif()

    set_target_properties(RDSMTests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_test(NAME RDSMTests COMMAND RDSMTests)
endif()
