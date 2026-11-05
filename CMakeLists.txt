cmake_minimum_required(VERSION 3.16)
project(Euphoria VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Euphoria Engine - a small 3D game engine (C++17 / OpenGL 3.3)
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Debug builds of math-heavy code (GLM, the physics solver, the BVH builder) are 10-50x slower
# than optimized ones, which shows up as low *and uneven* frame rates. For multi-config
# generators (the Visual Studio solution) inline small functions even in Debug so they stay
# usable; for real performance use the "release" preset (RelWithDebInfo).
if(MSVC)
    string(REPLACE "/Ob0" "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Every executable lands in build/bin so the editor can find the player when exporting.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(EUPHORIA_BUILD_EDITOR "Build the Euphoria Editor" ON)
option(EUPHORIA_BUILD_PLAYER "Build the standalone game player" ON)
option(EUPHORIA_BUILD_TESTS "Build the unit tests" ON)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(game)

if(EUPHORIA_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
if(EUPHORIA_BUILD_PLAYER)
    add_subdirectory(player)
endif()
if(EUPHORIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(MSVC AND EUPHORIA_BUILD_EDITOR)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT EuphoriaEditor)
endif()
