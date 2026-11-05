add_executable(EuphoriaTests EngineTests.cpp)
# The game's scripts are linked in too, so gameplay systems (doors, interaction) get tested.
target_link_libraries(EuphoriaTests PRIVATE EuphoriaEngine EuphoriaGame)
if(MSVC)
    target_compile_options(EuphoriaTests PRIVATE /utf-8)
endif()
add_test(NAME EngineTests COMMAND EuphoriaTests)
set_target_properties(EuphoriaTests PROPERTIES OUTPUT_NAME "euphoria-tests")
