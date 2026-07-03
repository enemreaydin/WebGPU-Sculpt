#include "SdfRenderer.h"
#include "utils/FileHelper.h"
#include <iostream>

void SdfRenderer::SetProjectRoot(const std::filesystem::path& root)
{
    projectRoot = root;
}

void SdfRenderer::Initialize(wgpu::Device device, wgpu::TextureFormat format)
{
    renderTargetFormat = format;

    std::string coreShader = Utils::ReadFile(projectRoot / "assets/shaders/sdf_core.wgsl");
    std::string mainShader = Utils::ReadFile(projectRoot / "assets/shaders/main.wgsl");

    if (coreShader.empty() || mainShader.empty())
    {
        std::cerr << "[KRITIK]: Shader dosyalari okunamadi!\n";
        return;
    }

    std::string combinedShader = coreShader + "\n" + mainShader;

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.next = nullptr;
    wgslDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
    wgslDesc.code = combinedShader.c_str();

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<const wgpu::ChainedStruct*>(&wgslDesc);

    shaderModule = device.createShaderModule(shaderDesc);

    wgpu::BindGroupLayoutEntry bglEntry{};
    bglEntry.binding = 0;
    bglEntry.visibility = wgpu::ShaderStage::Fragment;
    bglEntry.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

    wgpu::BindGroupLayoutEntry bglEntryGlobals{};
    bglEntryGlobals.binding = 1;
    bglEntryGlobals.visibility = wgpu::ShaderStage::Fragment;
    bglEntryGlobals.buffer.type = wgpu::BufferBindingType::Uniform;

    wgpu::BindGroupLayoutEntry entries[2] = {
        bglEntry,
        bglEntryGlobals
    };

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;

    bindGroupLayout = device.createBindGroupLayout(bglDesc);

    if (!bindGroupLayout)
    {
        std::cerr << "[KRITIK]: BindGroupLayout olusturulamadi!\n";
        return;
    }

    wgpu::BufferDescriptor globalsDesc{};
    globalsDesc.size = sizeof(GlobalsData);
    globalsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    globalsDesc.mappedAtCreation = false;

    globalsBuffer = device.createBuffer(globalsDesc);

    if (!globalsBuffer)
    {
        std::cerr << "[KRITIK]: globalsBuffer olusturulamadi!\n";
        return;
    }

    WGPUBindGroupLayout bindGroupLayouts[1] = {
        bindGroupLayout
    };

    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = bindGroupLayouts;

    pipelineLayout = device.createPipelineLayout(layoutDesc);

    if (!pipelineLayout)
    {
        std::cerr << "[KRITIK]: PipelineLayout olusturulamadi!\n";
        return;
    }

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
        std::cerr << "[KRITIK]: RenderPipeline olusturulamadi!\n";
        return;
    }

    std::cout << "[BILGI]: Pipeline basariyla olusturuldu.\n";
}

void SdfRenderer::CreateSceneBuffer(
    wgpu::Device device,
    wgpu::Queue queue,
    const std::vector<SdfObjectData>& objects)
{
    std::vector<SdfObjectData> safeObjects = objects;

    if (safeObjects.empty())
    {
        SdfObjectData dummy{};
        dummy.type = -1;
        dummy.op = 0;
        dummy.position = { 0.0f, 0.0f, 0.0f, 0.0f };
        dummy.size = { 0.0f, 0.0f, 0.0f, 0.0f };
        dummy.color = { 0.0f, 0.0f, 0.0f, 0.0f };
        safeObjects.push_back(dummy);
    }

    if (!device || !queue || !bindGroupLayout || !globalsBuffer)
    {
        std::cerr << "[KRITIK]: CreateSceneBuffer icin gerekli kaynaklar eksik!\n";
        return;
    }

    if (bindGroup)
    {
        bindGroup.release();
        bindGroup = nullptr;
    }

    if (sceneBuffer)
    {
        sceneBuffer.release();
        sceneBuffer = nullptr;
    }

    uint64_t bufferSize = safeObjects.size() * sizeof(SdfObjectData);

    wgpu::BufferDescriptor bufferDesc{};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    bufferDesc.mappedAtCreation = false;

    sceneBuffer = device.createBuffer(bufferDesc);

    if (!sceneBuffer)
    {
        std::cerr << "[KRITIK]: sceneBuffer olusturulamadi!\n";
        return;
    }

    queue.writeBuffer(sceneBuffer, 0, safeObjects.data(), bufferSize);

    wgpu::BindGroupEntry bgEntry{};
    bgEntry.binding = 0;
    bgEntry.buffer = sceneBuffer;
    bgEntry.offset = 0;
    bgEntry.size = bufferSize;

    wgpu::BindGroupEntry bgEntryGlobals{};
    bgEntryGlobals.binding = 1;
    bgEntryGlobals.buffer = globalsBuffer;
    bgEntryGlobals.offset = 0;
    bgEntryGlobals.size = sizeof(GlobalsData);

    wgpu::BindGroupEntry bgEntries[2] = {
        bgEntry,
        bgEntryGlobals
    };

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;

    bindGroup = device.createBindGroup(bgDesc);

    if (!bindGroup)
    {
        std::cerr << "[KRITIK]: BindGroup olusturulamadi!\n";
        return;
    }

    std::cout << "[BILGI]: SceneBuffer ve BindGroup olusturuldu.\n";
}

void SdfRenderer::CreateRenderTarget(wgpu::Device device, uint32_t width, uint32_t height)
{
    if (!device || width == 0 || height == 0)
        return;

    if (width == renderWidth && height == renderHeight && renderTargetTexture)
        return;

    if (renderTargetView)
    {
        renderTargetView.release();
        renderTargetView = nullptr;
    }

    if (renderTargetTexture)
    {
        renderTargetTexture.release();
        renderTargetTexture = nullptr;
    }

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
    viewDesc.aspect = wgpu::TextureAspect::All;

    renderTargetView = renderTargetTexture.createView(viewDesc);

    renderWidth = width;
    renderHeight = height;
}

wgpu::TextureView SdfRenderer::GetRenderTargetView() const
{
    return renderTargetView;
}

uint32_t SdfRenderer::GetRenderWidth() const
{
    return renderWidth;
}

uint32_t SdfRenderer::GetRenderHeight() const
{
    return renderHeight;
}

void SdfRenderer::ClearSceneBuffer()
{
    if (bindGroup)
    {
        bindGroup.release();
        bindGroup = nullptr;
    }

    if (sceneBuffer)
    {
        sceneBuffer.release();
        sceneBuffer = nullptr;
    }
}

void SdfRenderer::Destroy()
{
    if (renderTargetView)
    {
        renderTargetView.release();
        renderTargetView = nullptr;
    }

    if (renderTargetTexture)
    {
        renderTargetTexture.release();
        renderTargetTexture = nullptr;
    }

    ClearSceneBuffer();

    if (globalsBuffer)
    {
        globalsBuffer.release();
        globalsBuffer = nullptr;
    }

    if (pipeline)
    {
        pipeline.release();
        pipeline = nullptr;
    }

    if (pipelineLayout)
    {
        pipelineLayout.release();
        pipelineLayout = nullptr;
    }

    if (bindGroupLayout)
    {
        bindGroupLayout.release();
        bindGroupLayout = nullptr;
    }

    if (shaderModule)
    {
        shaderModule.release();
        shaderModule = nullptr;
    }
}