#include "SdfRenderer.h"
#include "../utils/FileHelper.h"
#include <iostream>

void SdfRenderer::Initialize(wgpu::Device device, wgpu::TextureFormat format) {
    // 1. Shader Okuma
    std::string coreShader = Utils::ReadFile("src/scene/sdf_core.wgsl");
    std::string mainShader = Utils::ReadFile("assets/shaders/main.wgsl");

    if (coreShader.empty() || mainShader.empty()) {
        std::cerr << "[KRITIK]: Shader dosyalari okunamadi!\n";
        return;
    }

    std::string combinedShader = coreShader + "\n" + mainShader;

    // 2. Shader Modülü
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = combinedShader.c_str();

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<const wgpu::ChainedStruct*>(&wgslDesc);
    shaderModule = device.createShaderModule(shaderDesc);
    shaderModule = device.createShaderModule(shaderDesc);

    // 3. Bind Group Layout
    wgpu::BindGroupLayoutEntry bglEntry{};
    bglEntry.binding = 0;
    bglEntry.visibility = wgpu::ShaderStage::Fragment;
    bglEntry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bglEntry;
    bindGroupLayout = device.createBindGroupLayout(bglDesc);

    // 4. Pipeline Layout
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    // 1. Önce ham (raw) handle'ý al
    // Bu satýrý yukarýdaki 3 alternatifi deneyerek deðiþtir:
    WGPUBindGroupLayout rawBGL = static_cast<WGPUBindGroupLayout>(bindGroupLayout); 
    pipelineLayout = device.createPipelineLayout(layoutDesc);

    // 5. Pipeline
    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipeline = device.createRenderPipeline(pipelineDesc);
}

void SdfRenderer::CreateSceneBuffer(wgpu::Device device, wgpu::Queue queue, const std::vector<SdfObjectData>& objects) {
    if (objects.empty()) return;

    uint64_t bufferSize = objects.size() * sizeof(SdfObjectData);

    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    sceneBuffer = device.createBuffer(bufferDesc);

    queue.writeBuffer(sceneBuffer, 0, objects.data(), bufferSize);

    wgpu::BindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = sceneBuffer;
    bgEntry.size = bufferSize;

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    bindGroup = device.createBindGroup(bgDesc);
}

void SdfRenderer::Destroy() {
    if (sceneBuffer) sceneBuffer.release();
    if (bindGroup) bindGroup.release();
    if (pipeline) pipeline.release();
    if (pipelineLayout) pipelineLayout.release();
    if (bindGroupLayout) bindGroupLayout.release();
    if (shaderModule) shaderModule.release();
}