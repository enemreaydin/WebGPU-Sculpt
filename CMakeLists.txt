cmake_minimum_required(VERSION 3.20)
project(webgpu_project)
set(CMAKE_CXX_STANDARD 17)

include(deps/webgpu/webgpu.cmake)

if (NOT EMSCRIPTEN)
    add_subdirectory(deps/glfw)
else()
    add_library(glfw INTERFACE)
    target_link_options(glfw INTERFACE -sUSE_GLFW=3)
endif()

add_subdirectory(deps/glfw3webgpu)

add_executable(main src/main.cpp)
target_link_libraries(main PRIVATE webgpu glfw glfw3webgpu)
target_copy_webgpu_binaries(main)

configure_file(src/shader.wgsl shader.wgsl COPYONLY)