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

    // Binding 2: the compiled RPN CSG program (read-only storage).
    wgpu::BindGroupLayoutEntry bglEntryCsg{};
    bglEntryCsg.binding = 2;
    bglEntryCsg.visibility = wgpu::ShaderStage::Fragment;
    bglEntryCsg.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;

    wgpu::BindGroupLayoutEntry entries[3] = {
        bglEntry,
        bglEntryGlobals,
        bglEntryCsg
    };

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 3;
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

    wgpu::BufferDescriptor pickReadbackDesc{};
    pickReadbackDesc.size = 256;
    pickReadbackDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    pickReadbackDesc.mappedAtCreation = false;

    pickReadbackBuffer = device.createBuffer(pickReadbackDesc);

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

    wgpu::ColorTargetState objectIdTarget{};
    objectIdTarget.format = objectIdTargetFormat;
    objectIdTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::ColorTargetState colorTargets[2] = { colorTarget, objectIdTarget };

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 2;
    fragmentState.targets = colorTargets;

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
    const std::vector<SdfObjectData>& objects,
    const std::vector<CsgCommand>& csgProgram)
{
    std::vector<SdfObjectData> safeObjects = objects;

    if (safeObjects.empty())
    {
        SdfObjectData dummy{};
        dummy.type = -1;
        dummy.objectId = -1;
        dummy.parentId = -1;
        dummy.position = { 0.0f, 0.0f, 0.0f, 0.0f };
        dummy.rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        dummy.size = { 0.0f, 0.0f, 0.0f, 0.0f };
        dummy.color = { 0.0f, 0.0f, 0.0f, 0.0f };
        safeObjects.push_back(dummy);
    }

    // A storage buffer cannot be zero-sized. When the program is empty, upload a
    // single no-op command (an unknown cmd_type) so the shader leaves the stack empty
    // and falls back to the default background.
    std::vector<CsgCommand> safeProgram = csgProgram;
    if (safeProgram.empty())
    {
        CsgCommand noop{};
        noop.cmdType = static_cast<CsgCmdType>(-1);
        noop.operand = 0;
        noop.nodeId = -1;
        noop.pad0 = 0;
        safeProgram.push_back(noop);
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

    if (csgProgramBuffer)
    {
        csgProgramBuffer.release();
        csgProgramBuffer = nullptr;
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

    // CSG RPN program buffer (parallel to sceneBuffer).
    uint64_t programSize = safeProgram.size() * sizeof(CsgCommand);

    wgpu::BufferDescriptor programDesc{};
    programDesc.size = programSize;
    programDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    programDesc.mappedAtCreation = false;

    csgProgramBuffer = device.createBuffer(programDesc);

    if (!csgProgramBuffer)
    {
        std::cerr << "[KRITIK]: csgProgramBuffer olusturulamadi!\n";
        return;
    }

    queue.writeBuffer(csgProgramBuffer, 0, safeProgram.data(), programSize);

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

    wgpu::BindGroupEntry bgEntryCsg{};
    bgEntryCsg.binding = 2;
    bgEntryCsg.buffer = csgProgramBuffer;
    bgEntryCsg.offset = 0;
    bgEntryCsg.size = programSize;

    wgpu::BindGroupEntry bgEntries[3] = {
        bgEntry,
        bgEntryGlobals,
        bgEntryCsg
    };

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout;
    bgDesc.entryCount = 3;
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
    if (objectIdTargetView)
    {
        objectIdTargetView.release();
        objectIdTargetView = nullptr;
    }

    if (objectIdTargetTexture)
    {
        objectIdTargetTexture.release();
        objectIdTargetTexture = nullptr;
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

    wgpu::TextureDescriptor idTexDesc{};
    idTexDesc.size = { width, height, 1 };
    idTexDesc.format = objectIdTargetFormat;
    idTexDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    idTexDesc.dimension = wgpu::TextureDimension::_2D;
    idTexDesc.mipLevelCount = 1;
    idTexDesc.sampleCount = 1;

    objectIdTargetTexture = device.createTexture(idTexDesc);

    wgpu::TextureViewDescriptor idViewDesc{};
    idViewDesc.format = objectIdTargetFormat;
    idViewDesc.dimension = wgpu::TextureViewDimension::_2D;
    idViewDesc.mipLevelCount = 1;
    idViewDesc.arrayLayerCount = 1;
    idViewDesc.aspect = wgpu::TextureAspect::All;

    objectIdTargetView = objectIdTargetTexture.createView(idViewDesc);

    renderWidth = width;
    renderHeight = height;
}

wgpu::TextureView SdfRenderer::GetRenderTargetView() const
{
    return renderTargetView;
}

wgpu::TextureView SdfRenderer::GetObjectIdTargetView() const
{
    return objectIdTargetView;
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

    if (csgProgramBuffer)
    {
        csgProgramBuffer.release();
        csgProgramBuffer = nullptr;
    }
}

void SdfRenderer::ReadObjectIdAt(wgpu::Device device, wgpu::Queue queue, int x, int y,
                                   bool* outDone, uint32_t* outResult) {
    wgpu::ImageCopyTexture srcTexture = {};
    srcTexture.texture = objectIdTargetTexture;
    srcTexture.origin = { (uint32_t)x, (uint32_t)y, 0 };
    srcTexture.aspect = wgpu::TextureAspect::All;

    wgpu::ImageCopyBuffer dstBuffer = {};
    dstBuffer.buffer = pickReadbackBuffer;
    dstBuffer.layout.offset = 0;
    dstBuffer.layout.bytesPerRow = 256;
    dstBuffer.layout.rowsPerImage = 1;

    wgpu::Extent3D copySize = { 1, 1, 1 };

    wgpu::CommandEncoderDescriptor encoderDesc = {};
    wgpu::CommandEncoder encoder = device.createCommandEncoder(encoderDesc);
    encoder.copyTextureToBuffer(srcTexture, dstBuffer, copySize);

    wgpu::CommandBufferDescriptor cmdBufDesc = {};
    wgpu::CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();
    queue.submit(1, &cmdBuf);
    cmdBuf.release();

    wgpu::Buffer bufferRef = pickReadbackBuffer;

    // The returned unique_ptr owns the heap-allocated callback that the GPU
    // will invoke asynchronously. We must keep it alive until it fires, so we
    // store it in a member instead of discarding it. Discarding it freed the
    // std::function immediately, leaving wgpuBufferMapAsync holding a dangling
    // userdata pointer -> "table index is out of bounds" when the map completed.
    pickMapCallback = bufferRef.mapAsync(
        wgpu::MapMode::Read, 0, 256,
        [outDone, outResult, bufferRef](wgpu::BufferMapAsyncStatus status) mutable {
            if (status == wgpu::BufferMapAsyncStatus::Success) {
                const void* data = bufferRef.getConstMappedRange(0, 256);
                *outResult = *reinterpret_cast<const uint32_t*>(data);
                bufferRef.unmap();
            }
            *outDone = true;
        }
    );
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
    if (objectIdTargetView)
    {
        objectIdTargetView.release();
        objectIdTargetView = nullptr;
    }

    if (objectIdTargetTexture)
    {
        objectIdTargetTexture.release();
        objectIdTargetTexture = nullptr;
    }

    ClearSceneBuffer();

    if (globalsBuffer)
    {
        globalsBuffer.release();
        globalsBuffer = nullptr;
    }

    if (pickReadbackBuffer)
    {
        pickReadbackBuffer.release();
        pickReadbackBuffer = nullptr;
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