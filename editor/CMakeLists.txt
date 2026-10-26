file(GLOB_RECURSE NINEDIVINE_EDITOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(NineDivineEditor ${NINEDIVINE_EDITOR_SOURCES})
target_link_libraries(NineDivineEditor PRIVATE NineDivineEngine NineDivineGame)
target_include_directories(NineDivineEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(NineDivineEditor PRIVATE /utf-8 /MP)
endif()
# The editor exports games by copying the player, so build the player first.
if(TARGET NineDivinePlayer)
    add_dependencies(NineDivineEditor NineDivinePlayer)
endif()
set_target_properties(NineDivineEditor PROPERTIES OUTPUT_NAME "9divine-editor")
