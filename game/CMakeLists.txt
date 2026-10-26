# Your gameplay code lives here. Every .cpp in game/src is compiled into both the
# editor and the standalone player. Scripts register themselves with ND_SCRIPT(...),
# so this is an OBJECT library (a static library would let the linker drop them).
file(GLOB_RECURSE NINEDIVINE_GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(NineDivineGame OBJECT ${NINEDIVINE_GAME_SOURCES})
target_link_libraries(NineDivineGame PUBLIC NineDivineEngine)
target_include_directories(NineDivineGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(NineDivineGame PRIVATE /utf-8 /MP)
endif()
