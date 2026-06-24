cmake_minimum_required(VERSION 3.20)
project(webgpu_project)

set(CMAKE_CXX_STANDARD 17)

include(webgpu/webgpu.cmake)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE webgpu)