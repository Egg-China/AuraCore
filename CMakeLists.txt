cmake_minimum_required(VERSION 3.21)

project(AuraCore VERSION 0.1.0 LANGUAGES C CXX)

string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" AURACORE_IN_SOURCE_BUILD)
if(AURACORE_IN_SOURCE_BUILD)
    message(FATAL_ERROR "AuraCore must be built in a separate build directory.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Core Gui Network NetworkAuth Widgets Xml)
find_package(ZLIB REQUIRED)
find_package(LibArchive REQUIRED)
find_package(tomlplusplus 3.2.0 QUIET)
if(NOT tomlplusplus_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus
        GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(tomlplusplus)
endif()

add_subdirectory(launcher)
add_subdirectory(buildconfig)
add_subdirectory(libraries/javacheck)
add_subdirectory(libraries/libnbtplusplus)
add_subdirectory(libraries/murmur2)
add_subdirectory(libraries/qdcss)
add_subdirectory(libraries/launcher)

