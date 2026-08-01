#include "SdfRenderer.h"
#include "utils/FileHelper.h"
#include <algorithm>
#include <iostream>

namespace {
double SecondsSince(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// Stack-depth buckets a scene pipeline can be compiled for, ascending.
//
// Buckets rather than the exact depth because compiling a pipeline is expensive
// (tens to hundreds of ms in a browser) and a scene's required depth wobbles as the
// user edits: rounding up means adding one nested group does not trigger a rebuild.
//
// 🔴 The ladder starts at 8, NOT at the smallest depth a scene could use. Measured,
// on the benchmark suite, same programs, only the declared array size varying:
//
//        scene   depth 4   depth 8   depth 16
//        D         330       190       300
//        G         450       440       720
//        H       ~8500      3200     ~8500
//
// Cost is NOT monotonic in stack size. 16 is expensive (occupancy), and 4 is worse
// still — some codegen cliff in how a small dynamically-indexed array is lowered.
// Sizing the stack to what a scene "needs" is therefore actively harmful below 8;
// what this ladder is for is letting a DEEP scene render correctly rather than
// silently losing pushes to the `sp < CSG_STACK_DEPTH` guard.
//
// Re-measure this table before changing the first entry.
constexpr int32_t kCsgStackDepthBuckets[] = {8, 16, 32 };
}

void SdfRenderer::SetProjectRoot(const std::filesystem::path& root)
{
    projectRoot = root;
}

std::string SdfRenderer::BuildShaderSource(int32_t stackDepth) const
{
    // Injected rather than written into the .wgsl: WGSL array sizes must be
    // const-expressions, so varying the stack depth means varying the source. WGSL
    // module scope is order-independent, so prepending the declaration is enough —
    // it does not have to sit near the arrays that use it.
    return "const CSG_STACK_DEPTH = " + std::to_string(stackDepth) + ";\n"
        + coreShaderSource + "\n" + mainShaderSource;
}

int32_t SdfRenderer::BucketForDepth(int32_t requiredStackDepth)
{
    for (const int32_t bucket : kCsgStackDepthBuckets)
    {
        if (requiredStackDepth <= bucket) return bucket;
    }
    return kMaxCsgStackDepth;
}

void SdfRenderer::EnsureScenePipeline(wgpu::Device device, int32_t requiredStackDepth)
{
    if (!device || !pipelineLayout)
    {
        return;
    }

    if (requiredStackDepth > kMaxCsgStackDepth)
    {
        // Clamping does not fix this: the shader's own `sp < CSG_STACK_DEPTH` guard
        // will drop the extra pushes and the scene renders with geometry missing and
        // no other symptom. Render the best available variant anyway, but be loud.
        std::cerr << "[SdfRenderer] WARNING: CSG program needs stack depth "
                  << requiredStackDepth << ", deepest variant is " << kMaxCsgStackDepth
                  << " — geometry will be silently dropped.\n";
    }

    const int32_t bucket = BucketForDepth(requiredStackDepth);
    if (pipeline && bucket == activeCsgStackDepth)
    {
        return;
    }

    const auto cached = scenePipelines.find(bucket);
    if (cached != scenePipelines.end())
    {
        pipeline = cached->second;
        activeCsgStackDepth = bucket;
        return;
    }

    // `source` must outlive the createShaderModule call — wgslDesc.code is a raw
    // pointer into it, not a copy.
    const std::string source = BuildShaderSource(bucket);

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.next = nullptr;
    wgslDesc.chain.sType = wgpu::SType::ShaderModuleWGSLDescriptor;
    wgslDesc.code = source.c_str();

    wgpu::ShaderModuleDescriptor shaderDesc{};
    shaderDesc.nextInChain = reinterpret_cast<const wgpu::ChainedStruct*>(&wgslDesc);

    wgpu::ShaderModule variantModule = device.createShaderModule(shaderDesc);
    if (!variantModule)
    {
        // Keep whatever pipeline is already bound: a stale-but-deep-enough variant
        // draws a correct image, an unbound pipeline draws nothing.
        std::cerr << "[SdfRenderer] shader module for CSG stack depth " << bucket
                  << " could not be created.\n";
        return;
    }

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = renderTargetFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::ColorTargetState objectIdTarget{};
    objectIdTarget.format = objectIdTargetFormat;
    objectIdTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::ColorTargetState colorTargets[2] = { colorTarget, objectIdTarget };

    wgpu::FragmentState fragmentState{};
    fragmentState.module = variantModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 2;
    fragmentState.targets = colorTargets;

    wgpu::RenderPipelineDescriptor pipelineDesc{};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = variantModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    wgpu::RenderPipeline variantPipeline = device.createRenderPipeline(pipelineDesc);
    if (!variantPipeline)
    {
        std::cerr << "[SdfRenderer] scene pipeline for CSG stack depth " << bucket
                  << " could not be created.\n";
        variantModule.release();
        return;
    }

    sceneShaderModules[bucket] = variantModule;
    scenePipelines[bucket] = variantPipeline;
    pipeline = variantPipeline;
    activeCsgStackDepth = bucket;

    std::cout << "[SdfRenderer] compiled scene pipeline for CSG stack depth "
              << bucket << "\n";
}

void SdfRenderer::Initialize(wgpu::Device device, wgpu::TextureFormat format)
{
    renderTargetFormat = format;

    coreShaderSource = Utils::ReadFile(projectRoot / "assets/shaders/sdf_core.wgsl");
    mainShaderSource = Utils::ReadFile(projectRoot / "assets/shaders/main.wgsl");

    if (coreShaderSource.empty() || mainShaderSource.empty())
    {
        std::cerr << "[KRITIK]: Shader dosyalari okunamadi!\n";
        return;
    }

    // Base module, built at the smallest bucket. Only fs_resolve is taken from it —
    // that entry point has no RPN stacks, so the depth it was specialised with is
    // irrelevant to it. The scene pipeline comes from EnsureScenePipeline below.
    const std::string combinedShader = BuildShaderSource(kCsgStackDepthBuckets[0]);

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

    // Builds (and warms) the smallest bucket. Every later scene edit routes through
    // the same call from SceneSerializer, so this is the only place a scene pipeline
    // is ever made.
    EnsureScenePipeline(device, 0);

    if (!pipeline)
    {
        std::cerr << "[KRITIK]: RenderPipeline olusturulamadi!\n";
        return;
    }

    // --- Resolve pipeline ---------------------------------------------------
    // A second, separate pipeline that runs fs_resolve: it averages the 4
    // accumulation layers into the swapchain and draws the gizmo on top. It has
    // its OWN bind-group layout (globals uniform + the accum texture array) —
    // the scene layout above is untouched.
    wgpu::BindGroupLayoutEntry resolveGlobalsEntry{};
    resolveGlobalsEntry.binding = 1;                       // matches `globals` in the shader
    resolveGlobalsEntry.visibility = wgpu::ShaderStage::Fragment;
    resolveGlobalsEntry.buffer.type = wgpu::BufferBindingType::Uniform;

    wgpu::BindGroupLayoutEntry resolveTexEntry{};
    resolveTexEntry.binding = 3;                           // matches `accumTex` in the shader
    resolveTexEntry.visibility = wgpu::ShaderStage::Fragment;
    resolveTexEntry.texture.sampleType = wgpu::TextureSampleType::Float;
    resolveTexEntry.texture.viewDimension = wgpu::TextureViewDimension::_2DArray;
    resolveTexEntry.texture.multisampled = false;

    wgpu::BindGroupLayoutEntry resolveEntries[2] = { resolveGlobalsEntry, resolveTexEntry };

    wgpu::BindGroupLayoutDescriptor resolveBglDesc{};
    resolveBglDesc.entryCount = 2;
    resolveBglDesc.entries = resolveEntries;
    resolveBindGroupLayout = device.createBindGroupLayout(resolveBglDesc);

    WGPUBindGroupLayout resolveLayouts[1] = { resolveBindGroupLayout };
    wgpu::PipelineLayoutDescriptor resolveLayoutDesc{};
    resolveLayoutDesc.bindGroupLayoutCount = 1;
    resolveLayoutDesc.bindGroupLayouts = resolveLayouts;
    resolvePipelineLayout = device.createPipelineLayout(resolveLayoutDesc);

    wgpu::RenderPipelineDescriptor resolvePipelineDesc{};
    resolvePipelineDesc.layout = resolvePipelineLayout;
    resolvePipelineDesc.vertex.module = shaderModule;
    resolvePipelineDesc.vertex.entryPoint = "vs_main";    // reuse the full-screen quad
    resolvePipelineDesc.vertex.bufferCount = 0;

    wgpu::ColorTargetState resolveColorTarget{};
    resolveColorTarget.format = format;                   // swapchain format, single target
    resolveColorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState resolveFragmentState{};
    resolveFragmentState.module = shaderModule;
    resolveFragmentState.entryPoint = "fs_resolve";
    resolveFragmentState.targetCount = 1;                 // one target: no object-id here
    resolveFragmentState.targets = &resolveColorTarget;
    resolvePipelineDesc.fragment = &resolveFragmentState;

    resolvePipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    resolvePipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    resolvePipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    resolvePipelineDesc.multisample.count = 1;
    resolvePipelineDesc.multisample.mask = ~0u;

    resolvePipeline = device.createRenderPipeline(resolvePipelineDesc);
    if (!resolvePipeline)
    {
        std::cerr << "[KRITIK]: ResolvePipeline olusturulamadi!\n";
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
        noop.parameter = 0.0f;
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
    sceneBufferSize = bufferSize;

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
    csgProgramBufferSize = programSize;

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

void SdfRenderer::UpdateSceneBuffer(
    wgpu::Device device,
    wgpu::Queue queue,
    const std::vector<SdfObjectData>& objects,
    const std::vector<CsgCommand>& csgProgram)
{
    const uint64_t objectBytes = objects.size() * sizeof(SdfObjectData);
    const uint64_t commandBytes = csgProgram.size() * sizeof(CsgCommand);
    if (!sceneBuffer || !csgProgramBuffer || objects.empty() || csgProgram.empty() ||
        objectBytes != sceneBufferSize || commandBytes != csgProgramBufferSize)
    {
        CreateSceneBuffer(device, queue, objects, csgProgram);
        return;
    }

    queue.writeBuffer(sceneBuffer, 0, objects.data(), objectBytes);
    queue.writeBuffer(csgProgramBuffer, 0, csgProgram.data(), commandBytes);
}

void SdfRenderer::UpdateCsgProgramBuffer(
    wgpu::Device device,
    wgpu::Queue queue,
    const std::vector<SdfObjectData>& objects,
    const std::vector<CsgCommand>& csgProgram)
{
    const uint64_t commandBytes = csgProgram.size() * sizeof(CsgCommand);
    if (!csgProgramBuffer || csgProgram.empty() || commandBytes != csgProgramBufferSize)
    {
        CreateSceneBuffer(device, queue, objects, csgProgram);
        return;
    }
    queue.writeBuffer(csgProgramBuffer, 0, csgProgram.data(), commandBytes);
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

    if (accumTexture)
    {
        accumTexture.release();
        accumTexture = nullptr;
    }
    if (accumArrayView)
    {
        accumArrayView.release();
        accumArrayView = nullptr;
    }
    for (int32_t i = 0; i < kAccumLayers; i++)
    {
        if (accumLayerViews[i])
        {
            accumLayerViews[i].release();
            accumLayerViews[i] = nullptr;
        }
        
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

    wgpu::TextureDescriptor accumTextureDesc{};
    accumTextureDesc.size = {width, height, kAccumLayers};
    accumTextureDesc.format = renderTargetFormat;
    accumTextureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    accumTextureDesc.dimension = wgpu::TextureDimension::_2D;
    accumTextureDesc.mipLevelCount = 1;
    accumTextureDesc.sampleCount = 1;

    accumTexture = device.createTexture(accumTextureDesc);

    for (int32_t i = 0; i < kAccumLayers; i++)
    {
        wgpu::TextureViewDescriptor accumLayerViewDesc{};
        accumLayerViewDesc.baseArrayLayer = i;
        accumLayerViewDesc.dimension = wgpu::TextureViewDimension::_2D;
        accumLayerViewDesc.mipLevelCount = 1;
        accumLayerViewDesc.arrayLayerCount = 1;
        accumLayerViews[i] = accumTexture.createView(accumLayerViewDesc);
    }

    wgpu::TextureViewDescriptor accumArrayViewDesc{};
    accumArrayViewDesc.baseArrayLayer = 0;
    accumArrayViewDesc.dimension = wgpu::TextureViewDimension::_2DArray;
    accumArrayViewDesc.mipLevelCount = 1;
    accumArrayViewDesc.arrayLayerCount = kAccumLayers;
    accumArrayView = accumTexture.createView(accumArrayViewDesc);

    // The resolve bind group points at accumArrayView, which was just recreated,
    // so it must be rebuilt here every time the render target is (re)sized.
    if (resolveBindGroup)
    {
        resolveBindGroup.release();
        resolveBindGroup = nullptr;
    }

    if (resolveBindGroupLayout && globalsBuffer && accumArrayView)
    {
        wgpu::BindGroupEntry resolveGlobalsBg{};
        resolveGlobalsBg.binding = 1;
        resolveGlobalsBg.buffer = globalsBuffer;
        resolveGlobalsBg.offset = 0;
        resolveGlobalsBg.size = sizeof(GlobalsData);

        wgpu::BindGroupEntry resolveTexBg{};
        resolveTexBg.binding = 3;
        resolveTexBg.textureView = accumArrayView;   // textures use .textureView, not .buffer

        wgpu::BindGroupEntry resolveBgEntries[2] = { resolveGlobalsBg, resolveTexBg };

        wgpu::BindGroupDescriptor resolveBgDesc{};
        resolveBgDesc.layout = resolveBindGroupLayout;
        resolveBgDesc.entryCount = 2;
        resolveBgDesc.entries = resolveBgEntries;
        resolveBindGroup = device.createBindGroup(resolveBgDesc);
    }
}

wgpu::TextureView SdfRenderer::GetAccumLayerView(uint32_t i) const
{
    if (i >= kAccumLayers) return nullptr;
    return accumLayerViews[i];
}

wgpu::TextureView SdfRenderer::GetAccumArrayView() const
{
    return accumArrayView;
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
    sceneBufferSize = 0;
    csgProgramBufferSize = 0;
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

bool SdfRenderer::StartFinalRender(wgpu::Device device, wgpu::Queue queue,
                                   const GlobalsData& snapshot, uint32_t width, uint32_t height)
{
    if (!device || !queue || !pipeline || !sceneBuffer || !csgProgramBuffer ||
        finalRenderState == 1 || finalRenderState == 2 || width == 0 || height == 0)
        return false;

    ReleaseFinalRender();
    finalWidth = width;
    finalHeight = height;
    finalPaddedBytesPerRow = ((width * 4u + 255u) / 256u) * 256u;

    wgpu::TextureDescriptor colorDesc{};
    colorDesc.size = { width, height, 1 };
    colorDesc.format = renderTargetFormat;
    colorDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    colorDesc.dimension = wgpu::TextureDimension::_2D;
    colorDesc.mipLevelCount = 1;
    colorDesc.sampleCount = 1;
    finalTexture = device.createTexture(colorDesc);
    finalView = finalTexture.createView();

    wgpu::TextureDescriptor idDesc = colorDesc;
    idDesc.format = objectIdTargetFormat;
    idDesc.usage = wgpu::TextureUsage::RenderAttachment;
    finalIdTexture = device.createTexture(idDesc);
    finalIdView = finalIdTexture.createView();

    wgpu::BufferDescriptor globalsDesc{};
    globalsDesc.size = sizeof(GlobalsData);
    globalsDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    finalGlobalsBuffer = device.createBuffer(globalsDesc);
    queue.writeBuffer(finalGlobalsBuffer, 0, &snapshot, sizeof(snapshot));

    wgpu::BindGroupEntry entries[3]{};
    entries[0].binding = 0; entries[0].buffer = sceneBuffer; entries[0].size = sceneBufferSize;
    entries[1].binding = 1; entries[1].buffer = finalGlobalsBuffer; entries[1].size = sizeof(GlobalsData);
    entries[2].binding = 2; entries[2].buffer = csgProgramBuffer; entries[2].size = csgProgramBufferSize;
    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = bindGroupLayout; bgDesc.entryCount = 3; bgDesc.entries = entries;
    finalBindGroup = device.createBindGroup(bgDesc);

    wgpu::BufferDescriptor readDesc{};
    readDesc.size = static_cast<uint64_t>(finalPaddedBytesPerRow) * height;
    readDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    finalReadbackBuffer = device.createBuffer(readDesc);
    if (!finalTexture || !finalView || !finalIdTexture || !finalIdView ||
        !finalGlobalsBuffer || !finalBindGroup || !finalReadbackBuffer) {
        ReleaseFinalRender();
        return false;
    }

    // Band setup. The first band is a deliberately small probe: it measures how
    // fast this GPU shades THIS scene, and every later band is sized from that
    // measurement (NextBandRows). Nothing is assumed about the hardware.
    finalRowsDone = 0;
    finalBandRows = 0;
    finalNextBandRows = std::min(kBandFirstRows, height);
    finalBandInFlight = false;
    finalToken = std::make_shared<bool>(true);
    // Pin the scene pipeline for the whole export. An export spans many frames and
    // many submits, and any scene edit in between can re-point `pipeline` at a
    // different stack-depth variant — bands drawn with mismatched shaders would give
    // an image whose halves disagree.
    finalPipeline = pipeline;
    finalRenderState = 1;
    SubmitNextFinalBand(device, queue);
    return true;
}

void SdfRenderer::SubmitNextFinalBand(wgpu::Device device, wgpu::Queue queue)
{
    const uint32_t y0 = finalRowsDone;
    const uint32_t bandH = std::min(finalNextBandRows, finalHeight - y0);

    wgpu::RenderPassColorAttachment attachments[2]{};
    attachments[0].view = finalView;
    // The first band clears the whole target; later bands must Load so the rows
    // the earlier bands already shaded survive the pass.
    attachments[0].loadOp = y0 == 0 ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
    attachments[0].storeOp = wgpu::StoreOp::Store;
    attachments[0].clearValue = WGPUColor{0.1, 0.1, 0.12, 1.0};
    attachments[1].view = finalIdView;
    attachments[1].loadOp = wgpu::LoadOp::Clear;
    attachments[1].storeOp = wgpu::StoreOp::Discard;
    attachments[1].clearValue = WGPUColor{4294967295.0, 0, 0, 0};
#ifdef __EMSCRIPTEN__
    attachments[0].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    attachments[1].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif
    wgpu::RenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 2; passDesc.colorAttachments = attachments;
    wgpu::CommandEncoder encoder = device.createCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.beginRenderPass(passDesc);
    pass.setPipeline(finalPipeline);
    pass.setBindGroup(0, finalBindGroup, 0, nullptr);
    // Same full-screen quad, same shader, same uniforms as before — the
    // scissor is what restricts rasterization (and therefore all fragment
    // work) to this band's rows. fs_main derives each ray purely from the
    // fragment's own pixel coordinate, so shading a sub-rectangle in a
    // separate pass produces bit-identical pixels to the single-draw version.
    pass.setScissorRect(0, y0, finalWidth, bandH);
    pass.draw(6, 1, 0, 0);
    pass.end(); pass.release();
    wgpu::CommandBuffer commands = encoder.finish(); encoder.release();
    queue.submit(1, &commands); commands.release();

    finalBandRows = bandH;
    finalBandInFlight = true;
    finalPhaseStart = std::chrono::steady_clock::now();
    // Fires when the GPU has finished everything submitted up to this point.
    // Only flip flags and measure here; the next band is submitted from
    // StepFinalRender. The token guards a late fire from an export that has
    // already ended (see RetireFinalCallbacks).
    auto token = finalToken;
    finalBandCallback = queue.onSubmittedWorkDone(
        [this, token](wgpu::QueueWorkDoneStatus status) {
            if (!*token) return;
            finalBandInFlight = false;
            if (status != wgpu::QueueWorkDoneStatus::Success) { finalRenderState = 4; return; }
            const double seconds = SecondsSince(finalPhaseStart);
            finalRowsDone += finalBandRows;
            finalNextBandRows = NextBandRows(finalBandRows, seconds);
        });
}

// Scale the band height so the next band lands near kBandTargetSeconds.
uint32_t SdfRenderer::NextBandRows(uint32_t rows, double seconds) const
{
    // A band too fast to time is simply doubled rather than extrapolated to
    // absurdity from a near-zero denominator.
    double scaled = (seconds > 0.0001)
        ? static_cast<double>(rows) * (kBandTargetSeconds / seconds)
        : static_cast<double>(rows) * 2.0;
    // Grow slowly, shrink instantly: overshooting risks a watchdog reset that
    // kills the whole export, while undershooting only costs an extra submit.
    scaled = std::min(scaled, static_cast<double>(rows) * 2.0);
    const uint32_t next = static_cast<uint32_t>(std::max(1.0, scaled));
    return std::clamp(next, kBandMinRows, kBandMaxRows);
}

void SdfRenderer::StepFinalRender(wgpu::Device device, wgpu::Queue queue)
{
    const bool waitingOnBand = finalRenderState == 1 && finalBandInFlight;
    const bool waitingOnMap  = finalRenderState == 2 && finalMapInFlight;

    // Stall watchdog. A GPU reset (Windows TDR) destroys the work in flight and
    // its completion callback is never delivered, so without this the export
    // would sit at the same percentage forever with no error. Report the
    // failure instead; the UI surfaces it and the JS poll loop can stop.
    if ((waitingOnBand || waitingOnMap) && SecondsSince(finalPhaseStart) > kStallSeconds) {
        finalRenderState = 4;
        return;
    }

    if (finalRenderState != 1 || finalBandInFlight) return;

    if (finalRowsDone < finalHeight) SubmitNextFinalBand(device, queue);
    else StartFinalReadback(device, queue);
}

void SdfRenderer::StartFinalReadback(wgpu::Device device, wgpu::Queue queue)
{
    const uint32_t width = finalWidth;
    const uint32_t height = finalHeight;
    wgpu::CommandEncoder encoder = device.createCommandEncoder();
    wgpu::ImageCopyTexture src{}; src.texture = finalTexture;
    wgpu::ImageCopyBuffer dst{}; dst.buffer = finalReadbackBuffer;
    dst.layout.bytesPerRow = finalPaddedBytesPerRow; dst.layout.rowsPerImage = height;
    encoder.copyTextureToBuffer(src, dst, {width, height, 1});
    wgpu::CommandBuffer commands = encoder.finish(); encoder.release();
    queue.submit(1, &commands); commands.release();

    finalRenderState = 2;
    finalMapInFlight = true;
    finalPhaseStart = std::chrono::steady_clock::now();
    const uint64_t mappedSize = static_cast<uint64_t>(finalPaddedBytesPerRow) * height;
    wgpu::Buffer bufferRef = finalReadbackBuffer;
    auto token = finalToken;
    finalMapCallback = bufferRef.mapAsync(wgpu::MapMode::Read, 0, mappedSize,
        [this, bufferRef, mappedSize, token](wgpu::BufferMapAsyncStatus status) mutable {
            if (!*token) return;
            finalMapInFlight = false;
            if (status != wgpu::BufferMapAsyncStatus::Success) { finalRenderState = 4; return; }
            const auto* source = static_cast<const uint8_t*>(bufferRef.getConstMappedRange(0, mappedSize));
            finalPixels.resize(static_cast<size_t>(finalWidth) * finalHeight * 4u);
            for (uint32_t y = 0; y < finalHeight; ++y) {
                const uint8_t* row = source + static_cast<size_t>(y) * finalPaddedBytesPerRow;
                uint8_t* out = finalPixels.data() + static_cast<size_t>(y) * finalWidth * 4u;
                for (uint32_t x = 0; x < finalWidth; ++x) {
                    // The configured browser surface is BGRA8; expose portable RGBA.
                    out[x*4+0] = row[x*4+2]; out[x*4+1] = row[x*4+1];
                    out[x*4+2] = row[x*4+0]; out[x*4+3] = row[x*4+3];
                }
            }
            bufferRef.unmap();
            finalRenderState = 3;
        });
}

int SdfRenderer::GetFinalRenderProgress() const
{
    if (finalRenderState >= 2) return 100;   // reading / ready
    if (finalHeight == 0) return 0;
    return static_cast<int>((100ull * finalRowsDone) / finalHeight);
}

// End the current export's callback ownership. Anything the GPU may still fire
// is parked (not freed) and neutered via the shared token — freeing a pending
// callback leaves the driver holding a raw pointer into released memory.
void SdfRenderer::RetireFinalCallbacks()
{
    if (finalToken) { *finalToken = false; finalToken.reset(); }

    if (finalBandInFlight && finalBandCallback)
        finalRetiredWorkCallbacks.push_back(std::move(finalBandCallback));
    finalBandCallback.reset();

    if (finalMapInFlight && finalMapCallback)
        finalRetiredMapCallbacks.push_back(std::move(finalMapCallback));
    finalMapCallback.reset();

    finalBandInFlight = false;
    finalMapInFlight = false;
}

void SdfRenderer::ReleaseFinalRender()
{
    RetireFinalCallbacks();
    finalRowsDone = finalBandRows = finalNextBandRows = 0;
    // Non-owning alias into scenePipelines — drop it, never release it.
    finalPipeline = nullptr;
    if (finalBindGroup) { finalBindGroup.release(); finalBindGroup = nullptr; }
    if (finalGlobalsBuffer) { finalGlobalsBuffer.release(); finalGlobalsBuffer = nullptr; }
    if (finalReadbackBuffer) { finalReadbackBuffer.release(); finalReadbackBuffer = nullptr; }
    if (finalView) { finalView.release(); finalView = nullptr; }
    if (finalTexture) { finalTexture.release(); finalTexture = nullptr; }
    if (finalIdView) { finalIdView.release(); finalIdView = nullptr; }
    if (finalIdTexture) { finalIdTexture.release(); finalIdTexture = nullptr; }
    finalPixels.clear(); finalWidth = finalHeight = finalPaddedBytesPerRow = 0;
    finalRenderState = 0;
}

void SdfRenderer::Destroy()
{
    ReleaseFinalRender();
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

    if (accumArrayView)
    {
        accumArrayView.release();
        accumArrayView = nullptr;
    }
    for (int32_t i = 0; i < kAccumLayers; i++)
    {
        if (accumLayerViews[i])
        {
            accumLayerViews[i].release();
            accumLayerViews[i] = nullptr;
        }

    }

    if (accumTexture)
    {
        accumTexture.release();
        accumTexture = nullptr;
    }

    if (resolveBindGroup)
    {
        resolveBindGroup.release();
        resolveBindGroup = nullptr;
    }

    if (resolvePipeline)
    {
        resolvePipeline.release();
        resolvePipeline = nullptr;
    }

    if (resolvePipelineLayout)
    {
        resolvePipelineLayout.release();
        resolvePipelineLayout = nullptr;
    }

    if (resolveBindGroupLayout)
    {
        resolveBindGroupLayout.release();
        resolveBindGroupLayout = nullptr;
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

    // `pipeline` and `finalPipeline` are non-owning aliases into scenePipelines —
    // releasing them here as well as below would double-release. Drop the aliases
    // first, then release each cached variant exactly once.
    pipeline = nullptr;
    finalPipeline = nullptr;
    activeCsgStackDepth = 0;

    for (auto& entry : scenePipelines)
    {
        if (entry.second) entry.second.release();
    }
    scenePipelines.clear();

    for (auto& entry : sceneShaderModules)
    {
        if (entry.second) entry.second.release();
    }
    sceneShaderModules.clear();

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
