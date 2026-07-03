// Single translation unit that emits the webgpu.hpp wrapper implementation.
// This define must live in exactly one .cpp, otherwise the linker reports duplicate symbols.
#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
