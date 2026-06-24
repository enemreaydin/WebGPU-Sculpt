#include <iostream>
#include <webgpu/webgpu.h>

int main() {
    std::cout << "Program started" << std::endl;
    
    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;
    WGPUInstance instance = wgpuCreateInstance(&desc);

    if (!instance) {
        std::cerr << "Could not initialize WebGPU!" << std::endl;
        return 1;
    }

    std::cout << "WGPU instance: " << instance << std::endl;


    wgpuInstanceRelease(instance);
    std::cout << "Instance finished: " << instance << std::endl;
    return 0;
}