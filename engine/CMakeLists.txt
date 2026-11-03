file(GLOB_RECURSE EUPHORIA_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(EuphoriaEngine STATIC ${EUPHORIA_ENGINE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${EUPHORIA_ENGINE_SOURCES})

target_include_directories(EuphoriaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(EuphoriaEngine PUBLIC glfw glad glm nlohmann_json single_headers ufbx imgui)

# Where the engine's own assets (shaders, fonts) live during development.
# Exported games carry a copy in an "EngineAssets" folder next to the executable.
target_compile_definitions(EuphoriaEngine PUBLIC
    EU_ENGINE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    EU_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    GLFW_INCLUDE_NONE)

if(WIN32)
    target_compile_definitions(EuphoriaEngine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(EuphoriaEngine PUBLIC ws2_32 psapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(EuphoriaEngine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(MSVC)
    target_compile_options(EuphoriaEngine PRIVATE /W3 /MP /utf-8 /bigobj)
else()
    target_compile_options(EuphoriaEngine PRIVATE -Wall -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers)
endif()

# Single-header third-party implementations: don't report their warnings as ours.
if(MSVC)
    set_source_files_properties(src/euphoria/ThirdPartyImpl.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(src/euphoria/ThirdPartyImpl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
