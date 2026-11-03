add_executable(EuphoriaPlayer src/PlayerMain.cpp)
target_link_libraries(EuphoriaPlayer PRIVATE EuphoriaEngine EuphoriaGame)
if(MSVC)
    target_compile_options(EuphoriaPlayer PRIVATE /utf-8)
endif()

# Release builds on Windows are GUI apps (no console window); Debug keeps the console for logs.
if(MSVC)
    set_target_properties(EuphoriaPlayer PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    target_link_options(EuphoriaPlayer PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
endif()
set_target_properties(EuphoriaPlayer PROPERTIES OUTPUT_NAME "euphoria-player")
