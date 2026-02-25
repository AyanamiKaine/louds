cmake_minimum_required(VERSION 3.28)
project(louds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(doctest)

add_library(louds STATIC)
target_sources(louds
    PUBLIC FILE_SET CXX_MODULES FILES
        src/louds.cppm
    PRIVATE
        src/louds_impl.cpp
)
target_compile_features(louds PUBLIC cxx_std_23)

# Sane warning defaults for project sources.
if(MSVC)
    target_compile_options(louds PRIVATE /W4 /permissive-)
else()
    target_compile_options(louds PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)

install(TARGETS louds
    EXPORT louds-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET CXX_MODULES
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/louds/cxx_modules
)

install(EXPORT louds-targets
    NAMESPACE louds::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/louds
)

include(CMakePackageConfigHelpers)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/louds-config.cmake" [=[
include(CMakeFindDependencyMacro)
include("${CMAKE_CURRENT_LIST_DIR}/louds-targets.cmake")
]=])

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/louds-config.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/louds
)

include(CTest)

if(BUILD_TESTING)
    add_executable(example tests/example.cpp)
    target_compile_definitions(example PRIVATE DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
    target_link_libraries(example PRIVATE
        louds
        doctest::doctest
    )
    add_test(NAME example COMMAND example)
endif()
