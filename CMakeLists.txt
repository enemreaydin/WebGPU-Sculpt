cmake_minimum_required(VERSION 3.20)
project(webgpu_project)
set(CMAKE_CXX_STANDARD 17)

# compile_commands.json Ã¼ret (IntelliSense iÃ§in) - project'ten hemen sonra olmalÄ±
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# BAÄžIMLILIKLAR (deps/)
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

# --- GLFW <-> WebGPU kÃ¶prÃ¼sÃ¼ ---
add_subdirectory(deps/glfw3webgpu)

# --- Dear ImGui (arayÃ¼z) ---
# ImGui'nin CMake'i yok, kaynaklarÄ± elle derliyoruz
# NOT: Buraya SADECE ImGui'nin kendi dosyalarÄ± girer, bizim kodumuz GÄ°RMEZ
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
<<<<<<< HEAD
 "src/sdf/SDFNode.h")
=======
  "src/utils/FileHelper.cpp"  "src/render/SdfRenderer.cpp")
>>>>>>> origin/zeynep
target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)
target_link_libraries(imgui PUBLIC webgpu glfw)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

<<<<<<< HEAD
# ============================================================
# ANA UYGULAMA (src/)
# ============================================================
=======
# --- Font'u exe'nin yanýna kopyala ---
configure_file(assets/fonts/Inter_18pt-Regular.ttf fonts/Inter_18pt-Regular.ttf COPYONLY)

>>>>>>> origin/zeynep

# ============================================================
# 1. ANA UYGULAMA (Arayüz + WebGPU + SDF Motoru)
# ============================================================
add_executable(main
    src/main.cpp
    src/core/Application.cpp
<<<<<<< HEAD
    src/core/Application.h
    src/core/Settings.h
    src/ui/DrawGuiElements.cpp
    src/ui/Theme.cpp
    src/ui/Theme.h
 "src/sdf/SDFNode.h")

# src/ klasÃ¶rÃ¼nÃ¼ include path'e ekle ki "core/Application.h" yazabilelim
=======
    src/render/SdfRenderer.cpp
    src/utils/FileHelper.cpp
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)
# src/ klasörünü include path'e ekle ki "Application.h" yazabilelim
>>>>>>> origin/zeynep
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
    src/render/SdfRenderer.cpp
    src/utils/FileHelper.cpp
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)

# ============================================================
# DOSYA KOPYALAMA ÝÞLEMLERÝ
# ============================================================
# CMake'e wgsl shader dosyalarýný exe'nin yanýna kopyalamasýný söylüyoruz 
# (Tüm assets klasörünü shaderlar ve ui ikonlarýyla beraber kopyalar)
file(COPY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Assetleri build klasörüne kopyala
file(COPY ${CMAKE_SOURCE_DIR}/src/scene DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


# ============================================================
# DERLEME SONRASI - varlÄ±klarÄ± exe'nin yanÄ±na kopyala
# ============================================================
<<<<<<< HEAD

# WebGPU DLL'ini exe'nin yanÄ±na kopyala
target_copy_webgpu_binaries(main)

# Font'u exe'nin GERÃ‡EK konumuna kopyala
# $<TARGET_FILE_DIR:main> = exe'nin bulunduÄŸu klasÃ¶r (build/Debug/ veya build/)
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter_18pt-Regular.ttf
        $<TARGET_FILE_DIR:main>/fonts/Inter_18pt-Regular.ttf
    COMMENT "Font exe'nin yanina kopyalaniyor"
)

# Shader'Ä± da exe'nin yanÄ±na kopyala (SDF render'a geÃ§ince yorumdan Ã§Ä±kar)
# add_custom_command(TARGET main POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         ${CMAKE_SOURCE_DIR}/src/shaders/raymarch.wgsl
#         $<TARGET_FILE_DIR:main>/shader.wgsl
# )
=======
# WebGPU DLL'ini exe'nin yanýna kopyala
target_copy_webgpu_binaries(main)

>>>>>>> origin/zeynep
