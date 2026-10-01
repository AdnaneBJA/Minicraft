cmake_minimum_required(VERSION 3.25)

project(Minicraft
        VERSION 0.1.0
        DESCRIPTION "Shared-world 2D survival game"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

option(MINICRAFT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(MINICRAFT_BUILD_CLIENT "Build the SDL3 client" ON)
option(MINICRAFT_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(game-core)

if(MINICRAFT_BUILD_CLIENT)
    add_subdirectory(client)
endif()

if(MINICRAFT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(game-core/tests)
endif()
