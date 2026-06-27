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
# NOT: Buraya SADECE ImGui'nin kendi dosyalarý girer, bizim kodumuz deðil
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp          # DEMO ÝÇÝN, SONRADAN SÝLÝNECEK
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
)
target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)
target_link_libraries(imgui PUBLIC webgpu glfw)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

# --- Font'u exe'nin yanýna kopyala ---
configure_file(assets/fonts/Inter_18pt-Regular.ttf fonts/Inter_18pt-Regular.ttf COPYONLY)


# ============================================================
# 1. ANA UYGULAMA (Arayüz + WebGPU + SDF Motoru)
# ============================================================
add_executable(main
    src/main.cpp
    src/Application.cpp
    src/Application.h
    
    # ImGui Arayüz Dosyalarý
    src/ui/DrawGuiElements.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
    
    # SDF Mimari Dosyalarýmýz (Application.cpp bunlarý kullanýyor)
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)

# src/ klasörünü include path'e ekle ki "Application.h" yazabilelim
target_include_directories(main PRIVATE src)

# Kütüphaneleri ana programa baðlýyoruz
target_link_libraries(main PRIVATE
    webgpu
    glfw
    glfw3webgpu
    imgui
)


# ============================================================
# 2. SDF MÝMARÝSÝ TEST PROGRAMI (Sadece konsol testi için)
# ============================================================
add_executable(sdf_test_run
    sdf_test.cpp
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)


# ============================================================
# DOSYA KOPYALAMA ÝÞLEMLERÝ
# ============================================================
# CMake'e wgsl shader dosyalarýný exe'nin yanýna kopyalamasýný söylüyoruz 
# (Tüm assets klasörünü shaderlar ve ui ikonlarýyla beraber kopyalar)
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


# ============================================================
# DERLEME SONRASI
# ============================================================
# WebGPU DLL'ini exe'nin yanýna kopyala
target_copy_webgpu_binaries(main)