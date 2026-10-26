add_executable(NineDivinePlayer src/PlayerMain.cpp)
target_link_libraries(NineDivinePlayer PRIVATE NineDivineEngine NineDivineGame)
if(MSVC)
    target_compile_options(NineDivinePlayer PRIVATE /utf-8)
endif()

# Release builds on Windows are GUI apps (no console window); Debug keeps the console for logs.
if(MSVC)
    set_target_properties(NineDivinePlayer PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    target_link_options(NineDivinePlayer PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
endif()
set_target_properties(NineDivinePlayer PROPERTIES OUTPUT_NAME "9divine-player")
