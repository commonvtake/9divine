cmake_minimum_required(VERSION 3.16)
project(NineDivine VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# 9divine - a small 3D game engine (C++17 / OpenGL 3.3)
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Every executable lands in build/bin so the editor can find the player when exporting.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(NINEDIVINE_BUILD_EDITOR "Build the 9divine Editor" ON)
option(NINEDIVINE_BUILD_PLAYER "Build the standalone game player" ON)
option(NINEDIVINE_BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(game)

if(NINEDIVINE_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
if(NINEDIVINE_BUILD_PLAYER)
    add_subdirectory(player)
endif()
if(NINEDIVINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MSVC AND NINEDIVINE_BUILD_EDITOR)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT NineDivineEditor)
endif()
