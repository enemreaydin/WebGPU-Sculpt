#include "SdfRenderer.h"
#include "../utils/FileHelper.h"
#include <iostream>


void SdfRenderer::SetProjectRoot(const std::filesystem::path& root)
{
    projectRoot = root;
}

// std::cout << __FILE__ << ":" << __LINE__ << std::endl;

void SdfRenderer::Initialize(wgpu::Device device, wgpu::TextureFormat format) {
    // 1. Shader Okuma
    renderTargetFormat = format;
    std::string coreShader = Utils::ReadFile(projectRoot / "src/scene/sdf_core.wgsl");
    std::string mainShader = Utils::ReadFile(projectRoot / "assets/shaders/main.wgsl");

    if (coreShader.empty() || mainShader.empty()) {
        std::cerr << "[KRITIK]: Shader dosyalari okunamadi!\n";
        return;
    }

    std::string combinedShader = coreShader + "\n" + mainShader;

    // 2. Shader Modülü
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.next = nullptr;
    wgslDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor; // <- eksik olan satýr
    wgslDesc.code = combinedShader.c_str();

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<const wgpu::ChainedStruct*>(&wgslDesc);
    shaderModule = device.createShaderModule(shaderDesc);

    // 3. Bind Group Layout
    wgpu::BindGroupLayoutEntry bglEntry{};
    bglEntry.binding = 0;
    bglEntry.visibility = wgpu::ShaderStage::Fragment;
    bglEntry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

    wgpu::BindGroupLayoutEntry bglEntryGlobals{};
    bglEntryGlobals.binding = 1;
    bglEntryGlobals.visibility = wgpu::ShaderStage::Fragment;
    bglEntryGlobals.buffer.type = wgpu::BufferBindingType::Uniform;

    wgpu::BindGroupLayoutEntry entries[2] = { bglEntry, bglEntryGlobals };

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;
    bindGroupLayout = device.createBindGroupLayout(bglDesc);

 if (!bindGroupLayout) { std::cerr << "BindGroupLayout oluþturulamadý!\n"; return; }

    wgpu::BindGroupLayout layouts[] =
    {
        bindGroupLayout
    };
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = reinterpret_cast<WGPUBindGroupLayout const*>(layouts);
    pipelineLayout = device.createPipelineLayout(layoutDesc);

    if (!pipelineLayout)
    {
        std::cerr << "PipelineLayout oluþturulamadý!\n";
        return;
    }

    // 5. Pipeline
    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 0;

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
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipeline = device.createRenderPipeline(pipelineDesc);

    if (!pipeline)
    {
        std::cerr << "RenderPipeline oluþturulamadý!\n";
        return;
    }

    std::cout << "Pipeline baþarýyla oluþturuldu.\n";
}

void SdfRenderer::CreateSceneBuffer(wgpu::Device device, wgpu::Queue queue, const std::vector<SdfObjectData>& objects) {
    if (objects.empty()) return;

    // 1. GÜVENLÝK: Device yoksa iþleme girme
    if (!device) {
        std::cerr << "[KRÝTÝK]: CreateSceneBuffer çaðrýldý ama device geçerli deðil!" << std::endl;
        return;
    }

    // 2. GÜVENLÝK: Layout yoksa bu fonksiyonun çalýþmasý imkansýzdýr (Crash sebebi burasý olabilir)
    if (!bindGroupLayout) {
        std::cerr << "[KRÝTÝK]: bindGroupLayout NULL! "
            << "Initialize() fonksiyonunda bir sorun var, shader derlenmemiþ olabilir." << std::endl;
        return;
    }

    uint64_t bufferSize = objects.size() * sizeof(SdfObjectData);

    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;

    sceneBuffer = device.createBuffer(bufferDesc);

    // 3. GÜVENLÝK: Buffer oluþturulamadýysa kontrol et
    if (!sceneBuffer) {
        std::cerr << "[KRÝTÝK]: sceneBuffer oluþturulamadý!" << std::endl;
        return;
    }

    uint64_t bufferSizeGlobal = sizeof(GlobalsData);

    wgpu::BufferDescriptor bufferDescGlobal{};
    bufferDescGlobal.size = sizeof(GlobalsData);
    bufferDescGlobal.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    globalsBuffer = device.createBuffer(bufferDescGlobal);

    // 3. GÜVENLÝK: Buffer oluþturulamadýysa kontrol et
    if (!globalsBuffer) {
        std::cerr << "[KRÝTÝK]: globalsBuffer oluþturulamadý!" << std::endl;
        return;
    }
    GlobalsData initialGlobals{};
    initialGlobals.resolution[0] = 1000.0f;
    initialGlobals.resolution[1] = 800.0f;
    queue.writeBuffer(globalsBuffer, 0, &initialGlobals, bufferSizeGlobal);
    queue.writeBuffer(sceneBuffer, 0, objects.data(), bufferSize);

    wgpu::BindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = sceneBuffer;
    bgEntry.offset = 0;
    bgEntry.size = bufferSize;

    wgpu::BindGroupEntry bgEntryGlobals{};
    bgEntryGlobals.binding = 1;
    bgEntryGlobals.buffer = globalsBuffer;
    bgEntryGlobals.offset = 0;
    bgEntryGlobals.size = bufferSizeGlobal;

    wgpu::BindGroupEntry entries[2] = { bgEntry, bgEntryGlobals };

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;

    // 4. BindGroup oluþtur ve sonucu kontrol et
    bindGroup = device.createBindGroup(bgDesc);

    if (!bindGroup) {
        std::cerr << "[KRÝTÝK]: BindGroup oluþturulamadý!" << std::endl;
    }
    else {
        std::cout << "[BÝLGÝ]: Sahne tamponu ve BindGroup baþarýyla oluþturuldu." << std::endl;
    }
}

void SdfRenderer::Destroy() {
    if (sceneBuffer) sceneBuffer.release();
    if (bindGroup) bindGroup.release();
    if (pipeline) pipeline.release();
    if (pipelineLayout) pipelineLayout.release();
    if (bindGroupLayout) bindGroupLayout.release();
    if (shaderModule) shaderModule.release();
}

void SdfRenderer::CreateRenderTarget(wgpu::Device device, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    if (width == renderWidth && height == renderHeight && renderTargetTexture) return; // boyut deðiþmediyse yeniden oluþturma

    // Eski texture varsa serbest býrak (resize durumunda sýzýntý olmasýn)
    if (renderTargetView) renderTargetView.release();
    if (renderTargetTexture) renderTargetTexture.release();

    wgpu::TextureDescriptor texDesc{};
    texDesc.size = { width, height, 1 };
    texDesc.format = renderTargetFormat;
    texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    texDesc.dimension = wgpu::TextureDimension::_2D;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    renderTargetTexture = device.createTexture(texDesc);

    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.format = renderTargetFormat;
    viewDesc.dimension = wgpu::TextureViewDimension::_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    renderTargetView = renderTargetTexture.createView(viewDesc);

    renderWidth = width;
    renderHeight = height;
}