# Your gameplay code lives here. Every .cpp in game/src is compiled into both the
# editor and the standalone player. Scripts register themselves with EU_SCRIPT(...),
# so this is an OBJECT library (a static library would let the linker drop them).
file(GLOB_RECURSE EUPHORIA_GAME_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(EuphoriaGame OBJECT ${EUPHORIA_GAME_SOURCES})
target_link_libraries(EuphoriaGame PUBLIC EuphoriaEngine)
target_include_directories(EuphoriaGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(EuphoriaGame PRIVATE /utf-8 /MP)
endif()
