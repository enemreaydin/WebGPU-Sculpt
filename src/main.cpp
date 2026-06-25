#define WEBGPU_CPP_IMPLEMENTATION
#include <webgpu/webgpu.hpp>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <iostream>
#include <cassert>
#include <fstream>

using namespace wgpu;

class Application {
public:
    bool Initialize();
    void Terminate();
    void MainLoop();
    bool IsRunning();
    void InitializePipeline();

private:
    TextureView GetNextSurfaceTextureView();

private:
    GLFWwindow* window;
    Device device;
    Queue queue;
    Surface surface;
    TextureFormat surfaceFormat = TextureFormat::Undefined;
    RenderPipeline pipeline;
};

int main() {
    Application app;
    if (!app.Initialize()) return 1;
    while (app.IsRunning()) {
        app.MainLoop();
    }
    app.Terminate();
    return 0;
}

bool Application::Initialize() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    window = glfwCreateWindow(640, 480, "WebGPU Triangle", nullptr, nullptr);

    Instance instance = wgpuCreateInstance(nullptr);
    surface = glfwGetWGPUSurface(instance, window);

    RequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    Adapter adapter = instance.requestAdapter(adapterOpts);
    instance.release();

    DeviceDescriptor deviceDesc = {};
    deviceDesc.label = "My Device";
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = "Default Queue";
    deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, char const* message, void*) {
        std::cout << "Device lost: " << reason;
        if (message) std::cout << " (" << message << ")";
        std::cout << std::endl;
        };
    device = adapter.requestDevice(deviceDesc);

    device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
        std::cout << "WebGPU error: " << type;
        if (message) std::cout << " (" << message << ")";
        std::cout << std::endl;
        });

    queue = device.getQueue();

    SurfaceConfiguration config = {};
    config.width = 640;
    config.height = 480;
    config.usage = TextureUsage::RenderAttachment;
    surfaceFormat = surface.getPreferredFormat(adapter);
    config.format = surfaceFormat;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    config.device = device;
    config.presentMode = PresentMode::Fifo;
    config.alphaMode = CompositeAlphaMode::Auto;
    surface.configure(config);

    adapter.release();
    InitializePipeline();

    return true;
}

void Application::Terminate() {
    surface.unconfigure();
    queue.release();
    surface.release();
    device.release();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Application::MainLoop() {
    glfwPollEvents();

    TextureView targetView = GetNextSurfaceTextureView();
    if (!targetView) return;

    CommandEncoderDescriptor encoderDesc = {};
    encoderDesc.label = "My command encoder";
    CommandEncoder encoder = device.createCommandEncoder(encoderDesc);

    RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.resolveTarget = nullptr;
    colorAttachment.loadOp = LoadOp::Clear;
    colorAttachment.storeOp = StoreOp::Store;
    colorAttachment.clearValue = WGPUColor{ 0.9, 0.1, 0.2, 1.0 };

    RenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = nullptr;
    renderPassDesc.timestampWrites = nullptr;

    RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
    // Draw komutlarý buraya gelecek
    
    renderPass.setPipeline(pipeline);
    renderPass.draw(3, 1, 0, 0);
    renderPass.end();
    renderPass.release();

    CommandBufferDescriptor cmdBufDesc = {};
    cmdBufDesc.label = "Command buffer";
    CommandBuffer cmdBuf = encoder.finish(cmdBufDesc);
    encoder.release();

    queue.submit(1, &cmdBuf);
    cmdBuf.release();

    targetView.release();
    surface.present();

    device.poll(false);
}

bool Application::IsRunning() {
    return !glfwWindowShouldClose(window);
}

TextureView Application::GetNextSurfaceTextureView() {
    SurfaceTexture surfaceTexture;
    surface.getCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != SurfaceGetCurrentTextureStatus::Success)
        return nullptr;

    Texture texture = surfaceTexture.texture;

    TextureViewDescriptor viewDesc = {};
    viewDesc.label = "Surface texture view";
    viewDesc.format = texture.getFormat();
    viewDesc.dimension = TextureViewDimension::_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = TextureAspect::All;

    return texture.createView(viewDesc);
}

void Application::InitializePipeline() {

    // Shader dosyasýný oku
    std::ifstream file("shader.wgsl");
    std::string shaderSource((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // Shader module oluþtur
    ShaderModuleDescriptor shaderDesc = {};
    ShaderModuleWGSLDescriptor shaderCodeDesc = {};
    shaderCodeDesc.chain.next = nullptr;
    shaderCodeDesc.chain.sType = SType::ShaderModuleWGSLDescriptor;
    shaderDesc.nextInChain = &shaderCodeDesc.chain;
    shaderCodeDesc.code = shaderSource.c_str();
    ShaderModule shaderModule = device.createShaderModule(shaderDesc);

    // Pipeline descriptor
    RenderPipelineDescriptor pipelineDesc = {};

    // Vertex aþamasý - vertex buffer yok, koordinatlar shader içinde
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.buffers = nullptr;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    pipelineDesc.vertex.constantCount = 0;
    pipelineDesc.vertex.constants = nullptr;

    // Primitive aþamasý - vertex'leri nasýl birleþtir
    pipelineDesc.primitive.topology = PrimitiveTopology::TriangleList;  // her 3 vertex bir üçgen
    pipelineDesc.primitive.stripIndexFormat = IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace = FrontFace::CCW;                  // counter-clockwise = ön yüz
    pipelineDesc.primitive.cullMode = CullMode::None;                   // arka yüzleri gizleme

    // Fragment aþamasý
    FragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;

    // Blend state - alpha blending
    BlendState blendState = {};
    blendState.color.srcFactor = BlendFactor::SrcAlpha;
    blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
    blendState.color.operation = BlendOperation::Add;
    blendState.alpha.srcFactor = BlendFactor::Zero;
    blendState.alpha.dstFactor = BlendFactor::One;
    blendState.alpha.operation = BlendOperation::Add;

    // Color target - hangi texture'a çizileceði
    ColorTargetState colorTarget = {};
    colorTarget.format = surfaceFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = ColorWriteMask::All;

    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Depth/stencil yok
    pipelineDesc.depthStencil = nullptr;

    // Multisample - anti-aliasing yok
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    // Layout - bind group yok þimdilik
    pipelineDesc.layout = nullptr;

    // Pipeline oluþtur
    pipeline = device.createRenderPipeline(pipelineDesc);

    // Shader module artýk gerekmez
    shaderModule.release();
}