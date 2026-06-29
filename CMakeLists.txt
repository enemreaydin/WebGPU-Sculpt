cmake_minimum_required(VERSION 3.20)
project(webgpu_project)
set(CMAKE_CXX_STANDARD 17)

# ============================================================
# BAĞIMLILIKLAR (deps/)
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
# ImGui'nin CMake'i yok, kaynakları elle derliyoruz
# NOT: Buraya SADECE ImGui'nin kendi dosyaları girer, bizim kodumuz değil
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp          # DEMO İÇİN, SONRADAN SİLİNECEK
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
 "src/Settings.h")
target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)
target_link_libraries(imgui PUBLIC webgpu glfw)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

# ============================================================
# ANA UYGULAMA (src/)
# ============================================================

add_executable(main
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/ui/DrawGuiElements.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
 "src/Settings.h")

# src/ klasörünü include path'e ekle ki "Application.h" yazabilelim
target_include_directories(main PRIVATE src)

target_link_libraries(main PRIVATE
    webgpu
    glfw
    glfw3webgpu
    imgui
)

# ============================================================
# DERLEME SONRASI - varlıkları exe'nin yanına kopyala
# ============================================================

# WebGPU DLL'ini exe'nin yanına kopyala
target_copy_webgpu_binaries(main)

# Font'u exe'nin GERÇEK konumuna kopyala
# $<TARGET_FILE_DIR:main> = exe'nin bulunduğu klasör (build/Debug/ veya build/)
# configure_file build köküne koyuyordu ama exe alt klasörde çalışıyor, bu yüzden custom command
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter_18pt-Regular.ttf
        $<TARGET_FILE_DIR:main>/fonts/Inter_18pt-Regular.ttf
    COMMENT "Font exe'nin yanina kopyalaniyor"
)

# Shader'ı da exe'nin yanına kopyala (SDF render'a geçince lazım olacak, şimdilik varsa)
# add_custom_command(TARGET main POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         ${CMAKE_SOURCE_DIR}/src/shaders/raymarch.wgsl
#         $<TARGET_FILE_DIR:main>/shader.wgsl
# )

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)