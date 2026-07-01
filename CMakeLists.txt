cmake_minimum_required(VERSION 3.20)
project(webgpu_project)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# BAGIMLILIKLAR
# ============================================================

include(deps/webgpu/webgpu.cmake)

if (NOT EMSCRIPTEN)
    add_subdirectory(deps/glfw)
else()
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_subdirectory(deps/glfw3webgpu)

# GLM submodule olarak geldiyse kullan
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
# ORTAK PROJE KAYNAKLARI
# ============================================================

set(SDF_CORE_SOURCES
    src/core/WebGPUImpl.cpp
    src/render/SdfRenderer.cpp
    src/utils/FileHelper.cpp
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)

# ============================================================
# ANA UYGULAMA
# ============================================================

add_executable(main
    src/main.cpp
    src/core/Application.cpp
    src/core/Application.h
    src/core/Settings.h
    src/ui/DrawGuiElements.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
    ${SDF_CORE_SOURCES}
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
)

if (TARGET glm::glm)
    target_link_libraries(main PRIVATE glm::glm)
endif()

# ============================================================
# SDF TEST PROGRAMI
# ============================================================

# add_executable(sdf_test_run
#     tests/sdf_test.cpp
#     ${SDF_CORE_SOURCES}
# )
#
# target_include_directories(sdf_test_run PRIVATE
#     src
#     deps/glm
# )
#
# target_link_libraries(sdf_test_run PRIVATE
#     webgpu
# )
#
# if (TARGET glm::glm)
#     target_link_libraries(sdf_test_run PRIVATE glm::glm)
# endif()

# ============================================================
# BDF TEST FILE GENERATOR
# ============================================================

add_executable(create_test_bdf
    src/tests/create_test_bdf.cpp
    src/io/ExtensionLoader.cpp
    src/scene/SceneManager.cpp
)

target_include_directories(create_test_bdf PRIVATE
    src
)

if (TARGET glm::glm)
    target_link_libraries(create_test_bdf PRIVATE glm::glm)
endif()

# ============================================================
# DERLEME SONRASI KOPYALAMA
# ============================================================

target_copy_webgpu_binaries(main)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter_18pt-Regular.ttf
        $<TARGET_FILE_DIR:main>/fonts/Inter_18pt-Regular.ttf
    COMMENT "Font exe yanina kopyalaniyor"
)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:main>/assets
    COMMENT "assets klasoru exe yanina kopyalaniyor"
)

add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/src
        $<TARGET_FILE_DIR:main>/src
    COMMENT "src klasoru exe yanina kopyalaniyor"
)