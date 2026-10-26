# Third-party libraries, all vendored so the engine builds offline.

# --- GLFW (window, input, gamepads) ---------------------------------------
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)   # X11 (works under XWayland too)
add_subdirectory(glfw)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# --- glad (OpenGL 3.3 loader, header-only; implemented in engine) ---------
add_library(glad INTERFACE)
target_include_directories(glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)

# --- GLM (math) -------------------------------------------------------------
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glm)
target_compile_definitions(glm INTERFACE GLM_ENABLE_EXPERIMENTAL GLM_FORCE_SILENT_WARNINGS)

# --- nlohmann/json (serialization) -----------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/json/include)

# --- Single-header libs (implemented in engine/src/cy/ThirdPartyImpl.cpp) ---
add_library(single_headers INTERFACE)
target_include_directories(single_headers INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/cgltf
    ${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader
    ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)

# --- ufbx (FBX import) -------------------------------------------------------
add_library(ufbx STATIC ufbx/ufbx.c)
target_include_directories(ufbx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ufbx)
set_target_properties(ufbx PROPERTIES FOLDER "ThirdParty" POSITION_INDEPENDENT_CODE ON)

# --- Dear ImGui (docking) + ImGuizmo ---------------------------------------
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_opengl3.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
    imguizmo/ImGuizmo.cpp)
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imguizmo)
target_link_libraries(imgui PUBLIC glfw)
target_compile_definitions(imgui PUBLIC GLFW_INCLUDE_NONE)
set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")
