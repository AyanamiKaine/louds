cmake_minimum_required(VERSION 3.28)
project(louds LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
