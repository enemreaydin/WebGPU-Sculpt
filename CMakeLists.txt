cmake_minimum_required(VERSION 3.20)
project(webgpu_project)
set(CMAKE_CXX_STANDARD 17)

# compile_commands.json üret (IntelliSense için) - project'ten hemen sonra olmalı
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# --- GLM (matematik kütüphanesi, SDF hesaplarında kullanılıyor) ---
add_subdirectory(deps/glm)

# --- Dear ImGui (arayüz) ---
# ImGui'nin CMake'i yok, kaynakları elle derliyoruz.
# NOT: Buraya SADECE ImGui'nin kendi dosyaları girer, bizim kodumuz GİRMEZ.
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_demo.cpp          # DEMO İÇİN, SONRADAN SİLİNECEK
    deps/imgui/backends/imgui_impl_glfw.cpp
    deps/imgui/backends/imgui_impl_wgpu.cpp
)
target_include_directories(imgui PUBLIC
    deps/imgui
    deps/imgui/backends
)
target_link_libraries(imgui PUBLIC webgpu glfw)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)

# ============================================================
# PROJE KAYNAK DOSYALARI (src/)
# ============================================================

# SDF mantığını oluşturan dosyalar - hem ana uygulamada hem konsol test
# programında kullanılıyor, o yüzden tek yerden tanımlayıp ikisine de
# paylaştırıyoruz (aynı listeyi iki kere yazmamak için).
set(SDF_CORE_SOURCES
    src/core/WebGPUImpl.cpp 
    src/render/SdfRenderer.cpp
    src/utils/FileHelper.cpp
    src/scene/SceneManager.cpp
    src/io/ExtensionLoader.cpp
)

# ============================================================
# 1. ANA UYGULAMA (Arayüz + WebGPU + SDF Motoru)
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

# src/ klasörünü include path'e ekle ki "core/Application.h" yazabilelim
target_include_directories(main PRIVATE src)

target_link_libraries(main PRIVATE
    webgpu
    glfw
    glfw3webgpu
    imgui
    glm::glm
)

# ============================================================
# 2. SDF MİMARİSİ TEST PROGRAMI (Sadece konsol testi için)
# ============================================================
# NOT: WebGPU/GLFW/ImGui'ye bağlı değil - sadece SDF mantığını
# (evalSDF, ağaç/sahne yapısı) pencere açmadan test etmek için.
# TODO: SdfRenderer.cpp WebGPU tiplerine dokunuyorsa buraya
# target_link_libraries(sdf_test_run PRIVATE webgpu) eklemek gerekebilir.
add_executable(sdf_test_run
    sdf_test.cpp
    ${SDF_CORE_SOURCES}
)
target_include_directories(sdf_test_run PRIVATE src)
target_link_libraries(sdf_test_run PRIVATE webgpu glm::glm)

# ============================================================
# DERLEME SONRASI - varlıkları exe'nin GERÇEK konumuna kopyala
# ============================================================
# $<TARGET_FILE_DIR:main> = exe'nin bulunduğu klasör (build/Debug/ veya build/)
# Visual Studio gibi multi-config generator'larda file(COPY ...) veya
# configure_file(...) komutları exe'nin yanına değil, build/ kökrüne
# kopyalar - bu yüzden hepsini POST_BUILD + TARGET_FILE_DIR ile yapıyoruz.

# WebGPU DLL'ini exe'nin yanına kopyala
target_copy_webgpu_binaries(main)

# Font'u kopyala
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:main>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter_18pt-Regular.ttf
        $<TARGET_FILE_DIR:main>/fonts/Inter_18pt-Regular.ttf
    COMMENT "Font exe'nin yanina kopyalaniyor"
)

# assets/ klasörünü (shader'lar, ui ikonları dahil) kopyala
add_custom_command(TARGET main POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:main>/assets
    COMMENT "assets/ exe'nin yanina kopyalaniyor"
)

# TODO: src/scene/ klasörünün build çıktısına kopyalanması gerekiyor muydu?
# Eğer içinde varsayılan sahne/preset dosyaları varsa asagidaki gibi
# tasinabilir; sadece kaynak kod ise (SceneManager.cpp gibi) kopyalanmamali.
# add_custom_command(TARGET main POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_directory
#         ${CMAKE_SOURCE_DIR}/src/scene
#         $<TARGET_FILE_DIR:main>/scene
#     COMMENT "scene/ exe'nin yanina kopyalaniyor"
# )