file(GLOB_RECURSE EUPHORIA_EDITOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(EuphoriaEditor ${EUPHORIA_EDITOR_SOURCES})
target_link_libraries(EuphoriaEditor PRIVATE EuphoriaEngine EuphoriaGame)
target_include_directories(EuphoriaEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(EuphoriaEditor PRIVATE /utf-8 /MP)
endif()
# The editor exports games by copying the player, so build the player first.
if(TARGET EuphoriaPlayer)
    add_dependencies(EuphoriaEditor EuphoriaPlayer)
endif()
set_target_properties(EuphoriaEditor PROPERTIES OUTPUT_NAME "euphoria-editor")
