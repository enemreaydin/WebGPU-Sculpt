cmake_minimum_required(VERSION 3.20)
project(webgpu_project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# DEPENDENCIES
# ============================================================

include(deps/webgpu/webgpu.cmake)

if (NOT EMSCRIPTEN)
    add_subdirectory(deps/glfw)
else()
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_subdirectory(deps/glfw3webgpu)

if (EXISTS "${CMAKE_SOURCE_DIR}/deps/glm/CMakeLists.txt")
    add_subdirectory(deps/glm)
endif()

# ============================================================
# IMGUI
# ============================================================

add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
)

target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)

target_link_libraries(imgui PUBLIC
    webgpu
    glfw
)

target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_WEBGPU_BACKEND_WGPU
)

# ============================================================
# IMGUIZMO
# ============================================================

add_library(imguizmo STATIC
    deps/imguizmo/src/ImGuizmo.cpp
)

target_include_directories(imguizmo PUBLIC
    deps/imguizmo/src
)

target_link_libraries(imguizmo PUBLIC
    imgui
)

# ============================================================
# PROJECT SOURCES
# ============================================================

# Editor layer: application, main loop, GUI panels
set(EDITOR_SOURCES
    src/editor/Application.cpp
    src/editor/Gui.cpp
    src/editor/MainMenuBar.cpp
    src/editor/SettingsPanel.cpp
    src/editor/Workspace.cpp
)

# Platform layer: window, graphics context, WebGPU wrapper implementation
set(PLATFORM_SOURCES
    src/platform/Window.cpp
    src/platform/GraphicsContext.cpp
    src/platform/WebGPUImpl.cpp
)

# SDF engine: rendering, scene, camera, serialization, utilities
set(ENGINE_SOURCES
    src/renderer/SdfRenderer.cpp
    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/scene/SceneManager.cpp
    src/scene/SdfObjectFactory.cpp
    src/scene/csg/CsgTree.cpp
    src/serialization/ExtensionLoader.cpp
    src/serialization/SceneSerializer.cpp
    src/utils/FileHelper.cpp
)

# Interaction: gizmo and selection controllers
set(INTERACTION_SOURCES
    src/interaction/GizmoController.cpp
)

# Reusable UI widgets
set(UI_SOURCES
    src/ui/Theme.cpp
    src/ui/AddObjectPanel.cpp
)

# Listed so headers show up in IDE project views
set(PROJECT_HEADERS
    src/platform/Window.h
    src/platform/GraphicsContext.h
    src/editor/Application.h
    src/editor/Settings.h
    src/scene/data/SdfEnums.h
    src/scene/data/SdfObjectData.h
    src/scene/data/GlobalsData.h
    src/scene/csg/CsgCommand.h
    src/scene/csg/CsgNode.h
    src/scene/csg/CsgTree.h
    src/renderer/SdfRenderer.h
    src/camera/Camera.h
    src/camera/CameraController.h
    src/scene/SceneManager.h
    src/scene/SdfObjectFactory.h
    src/interaction/GizmoController.h
    src/interaction/SelectionState.h
    src/serialization/ExtensionLoader.h
    src/serialization/SceneSerializer.h
    src/utils/FileHelper.h
    src/ui/Theme.h
    src/ui/AddObjectPanel.h
)

# ============================================================
# MAIN APPLICATION
# ============================================================

add_executable(main
    src/main.cpp
    ${EDITOR_SOURCES}
    ${PLATFORM_SOURCES}
    ${ENGINE_SOURCES}
    ${INTERACTION_SOURCES}
    ${UI_SOURCES}
    ${PROJECT_HEADERS}
)

target_include_directories(main PRIVATE
    src
    deps/imgui
    deps/imgui/backends
    deps/glm
)

target_link_libraries(main PRIVATE
    webgpu
    glfw
    glfw3webgpu
    imgui
    imguizmo
    glm
)

if (TARGET glm::glm)
    target_link_libraries(main PRIVATE glm::glm)
endif()

# ============================================================
# SDFO TEST FILE GENERATOR
# ============================================================

add_executable(create_test_sdfo
    src/tools/create_test_sdfo.cpp
    src/serialization/ExtensionLoader.cpp
    src/scene/SceneManager.cpp
    src/scene/csg/CsgTree.cpp
)

target_include_directories(create_test_sdfo PRIVATE
    src
)

if (TARGET glm::glm)
    target_link_libraries(create_test_sdfo PRIVATE glm::glm)
endif()

# ============================================================
# POST BUILD COPY
# ============================================================

target_copy_webgpu_binaries(main)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter_18pt-Regular.ttf
        $<TARGET_FILE_DIR:main>/fonts/Inter_18pt-Regular.ttf
    COMMENT "Copying font next to executable"
)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:main>/assets
    COMMENT "Copying assets next to executable"
)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src
        $<TARGET_FILE_DIR:main>/src
    COMMENT "Copying src next to executable"
)