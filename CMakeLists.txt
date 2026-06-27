cmake_minimum_required(VERSION 3.20)
project(webgpu_project)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# BAÐIMLILIKLAR (deps/)
# ============================================================

# --- WebGPU (wgpu-native backend) ---
include(deps/webgpu/webgpu.cmake)

# --- GLFW (pencere + input) ---
if (NOT EMSCRIPTEN)
    add_subdirectory(deps/glfw)
else()
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

# --- GLFW <-> WebGPU köprüsü ---
add_subdirectory(deps/glfw3webgpu)

# --- Dear ImGui (arayüz) ---
# ImGui'nin CMake'i yok, kaynaklarý elle derliyoruz
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    # Backend'ler: ImGui'yi GLFW ve WebGPU'ya baðlar
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
)
target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)
# ImGui'nin WebGPU backend'i webgpu ve glfw header'larýna ihtiyaç duyar
target_link_libraries(imgui PUBLIC webgpu glfw)
# wgpu-native backend kullandýðýmýzý ImGui'ye bildir
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

# ============================================================
# ANA UYGULAMA (src/)
# ============================================================

add_executable(main
    src/main.cpp
    src/Application.cpp
    src/Application.h
)

target_link_libraries(main PRIVATE
    webgpu
    glfw
    glfw3webgpu
    imgui
)

# ============================================================
# DERLEME SONRASI
# ============================================================

