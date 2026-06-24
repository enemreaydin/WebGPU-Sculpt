#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>  // glfwGetWGPUSurface için
#include <webgpu/webgpu.h>
#include <iostream>
#include <cassert>

#ifdef WEBGPU_BACKEND_WGPU
#  include <webgpu/wgpu.h>
#endif
// ============================================================
// ADAPTER REQUEST - async'i sync gibi sarýyor
// ============================================================
WGPUAdapter requestAdapterSync(WGPUInstance instance, WGPURequestAdapterOptions const* options) {
    struct UserData {
        WGPUAdapter adapter = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    // Adapter hazýr olunca bu lambda çaðrýlýr
    auto onAdapterRequestEnded = [](WGPURequestAdapterStatus status,
        WGPUAdapter adapter,
        char const* message,
        void* pUserData) {
            UserData& ud = *reinterpret_cast<UserData*>(pUserData);
            if (status == WGPURequestAdapterStatus_Success)
                ud.adapter = adapter;
            else
                std::cout << "Adapter alýnamadý: " << message << std::endl;
            ud.requestEnded = true;
        };

    wgpuInstanceRequestAdapter(instance, options, onAdapterRequestEnded, &userData);
    assert(userData.requestEnded);
    return userData.adapter;
}


// ============================================================
// DEVICE REQUEST - adapter'dan device alýr
// ============================================================
WGPUDevice requestDeviceSync(WGPUAdapter adapter, WGPUDeviceDescriptor const* descriptor) {
    struct UserData {
        WGPUDevice device = nullptr;
        bool requestEnded = false;
    };
    UserData userData;

    auto onDeviceRequestEnded = [](WGPURequestDeviceStatus status,
        WGPUDevice device,
        char const* message,
        void* pUserData) {
            UserData& ud = *reinterpret_cast<UserData*>(pUserData);
            if (status == WGPURequestDeviceStatus_Success)
                ud.device = device;
            else
                std::cout << "Device alýnamadý: " << message << std::endl;
            ud.requestEnded = true;
        };

    wgpuAdapterRequestDevice(adapter, descriptor, onDeviceRequestEnded, &userData);
    assert(userData.requestEnded);
    return userData.device;
}


// ============================================================
// MAIN
// ============================================================
int main() {

    // ----------------------------------------------------------
    // 1. INSTANCE - WebGPU'nun giriþ noktasý
    // ----------------------------------------------------------
    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = nullptr;
    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        std::cout << "Instance oluþturulamadý!" << std::endl;
        return 1;
    }


    // ----------------------------------------------------------
    // 2. GLFW WINDOW
    // ----------------------------------------------------------
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // OpenGL kullanmýyoruz
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);      // resize surface'ý etkiler, þimdilik kapat
    GLFWwindow* window = glfwCreateWindow(640, 480, "WebGPU Reference", nullptr, nullptr);


    // ----------------------------------------------------------
    // 3. SURFACE - GLFW penceresiyle WebGPU arasýndaki köprü
    // ----------------------------------------------------------
    WGPUSurface surface = glfwGetWGPUSurface(instance, window);


    // ----------------------------------------------------------
    // 4. ADAPTER - fiziksel GPU'yu temsil eder
    // ----------------------------------------------------------
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.nextInChain = nullptr;
    adapterOpts.compatibleSurface = surface;    // surface ile uyumlu adapter seç
    WGPUAdapter adapter = requestAdapterSync(instance, &adapterOpts);
    std::cout << "Adapter alindi: " << adapter << std::endl;

    // Instance artýk gerekmez
    wgpuInstanceRelease(instance);


    // ----------------------------------------------------------
    // 5. DEVICE - asýl çalýþma ortamý, tüm objeler buradan
    // ----------------------------------------------------------
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.nextInChain = nullptr;
    deviceDesc.label = "My Device";
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = "Default Queue";

    // Device kaybolunca (hata veya release) çaðrýlýr
    deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason,
        char const* message,
        void*) {
            std::cout << "Device lost: " << reason;
            if (message) std::cout << " (" << message << ")";
            std::cout << std::endl;
        };

    WGPUDevice device = requestDeviceSync(adapter, &deviceDesc);
    std::cout << "Device alindi: " << device << std::endl;

    // API'yi yanlýþ kullanýnca çaðrýlýr - hata ayýklamada çok iþe yarar
    wgpuDeviceSetUncapturedErrorCallback(device, [](WGPUErrorType type,
        char const* message,
        void*) {
            std::cout << "Uncaptured error: " << type;
            if (message) std::cout << " (" << message << ")";
            std::cout << std::endl;
        }, nullptr);

    // Adapter artýk gerekmez (device'ý aldýktan sonra býrakýlýr)
    wgpuAdapterRelease(adapter);


    // ----------------------------------------------------------
    // 6. QUEUE - CPU'dan GPU'ya komut gönderme kanalý
    // ----------------------------------------------------------
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // Queue iþi bitince çaðrýlýr
    wgpuQueueOnSubmittedWorkDone(queue, [](WGPUQueueWorkDoneStatus status, void*) {
        std::cout << "Queue is bitti, status: " << status << std::endl;
        }, nullptr);


    // ----------------------------------------------------------
    // 7. SURFACE CONFIGURATION - swap chain ayarlarý
    // ----------------------------------------------------------
    WGPUSurfaceConfiguration surfaceConfig = {};
    surfaceConfig.nextInChain = nullptr;
    surfaceConfig.device = device;
    surfaceConfig.width = 640;
    surfaceConfig.height = 480;
    surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    surfaceConfig.format = wgpuSurfaceGetPreferredFormat(surface, adapter);
    surfaceConfig.viewFormatCount = 0;
    surfaceConfig.viewFormats = nullptr;
    surfaceConfig.presentMode = WGPUPresentMode_Fifo;       // sýradaki frame'i göster
    surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(surface, &surfaceConfig);


    // ----------------------------------------------------------
    // 8. ANA DÖNGÜ
    // ----------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Surface texture al ---
        WGPUSurfaceTexture surfaceTexture;
        wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success)
            break;

        // --- Texture view oluþtur (render pass bunu hedef alýr) ---
        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.nextInChain = nullptr;
        viewDesc.label = "Surface texture view";
        viewDesc.format = wgpuTextureGetFormat(surfaceTexture.texture);
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

        // --- Command encoder oluþtur ---
        // GPU'ya gönderilecek komutlarý encode eder
        WGPUCommandEncoderDescriptor encoderDesc = {};
        encoderDesc.nextInChain = nullptr;
        encoderDesc.label = "Command encoder";
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        // --- Color attachment - render pass neyin üzerine çizecek ---
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = targetView;         // hedef texture
        colorAttachment.resolveTarget = nullptr;
        colorAttachment.loadOp = WGPULoadOp_Clear;   // pass baþlamadan ekraný temizle
        colorAttachment.storeOp = WGPUStoreOp_Store;  // sonucu kaydet
        colorAttachment.clearValue = WGPUColor{ 0.1, 0.3, 0.6, 1.0 }; // R G B A (mavi)

#ifndef WEBGPU_BACKEND_WGPU
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

        // --- Render pass - GPU'nun 3D render devresini kullanýr ---
        WGPURenderPassDescriptor renderPassDesc = {};
        renderPassDesc.nextInChain = nullptr;
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;
        renderPassDesc.depthStencilAttachment = nullptr;  // depth buffer yok þimdilik
        renderPassDesc.timestampWrites = nullptr;

        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
        // Buraya draw komutlarý gelecek (triangle, mesh vs.)
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);

        // --- Encoder'ý bitir ve command buffer üret ---
        WGPUCommandBufferDescriptor cmdBufferDesc = {};
        cmdBufferDesc.nextInChain = nullptr;
        cmdBufferDesc.label = "Command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
        wgpuCommandEncoderRelease(encoder);

        // --- Queue'ya gönder ---
        wgpuQueueSubmit(queue, 1, &command);
        wgpuCommandBufferRelease(command);

        // --- Frame'i ekranda göster ---
        wgpuTextureViewRelease(targetView);

#ifndef WEBGPU_BACKEND_WGPU
        wgpuTextureRelease(surfaceTexture.texture);
#endif

        wgpuSurfacePresent(surface);

#ifdef WEBGPU_BACKEND_WGPU
        wgpuTextureRelease(surfaceTexture.texture);
#endif

        // --- Device tick (Dawn'da callback'lerin iþlenmesi için gerekli) ---
#if defined(WEBGPU_BACKEND_DAWN)
        wgpuDeviceTick(device);
#elif defined(WEBGPU_BACKEND_WGPU)
        wgpuDevicePoll(device, false, nullptr);
#endif
    }


    // ----------------------------------------------------------
    // 9. CLEANUP - ters sýrada release
    // ----------------------------------------------------------
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuSurfaceRelease(surface);
    wgpuDeviceRelease(device);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}