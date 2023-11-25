#include "renderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include"tiny_gltf.h"

#include<iostream>
#include<fstream>
#include<string>
#include<set>
const uint64_t notimeout = std::numeric_limits<uint64_t>::max();
void Renderer::checkVkResult(VkResult result)
{
    if(result==VK_SUCCESS){
        return;
    }
    if(result<0){
        throw std::runtime_error("bad VkResult while using imgui!");
    }
    else{
        std::cerr<<"[vulkan] warning: VkResult = "<<result<<'\n';
    }
}
VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData)
{
    std::cerr<<pCallbackData->pMessage<<std::endl;
    return VK_FALSE;
}

Renderer::Renderer()
{
    init();
}

Renderer::~Renderer()
{
    cleanup();
}

void Renderer::init()
{
    initSDL();
    initVkInstance();
    initDLD();
    initDebugMessenger();
    initSurface();
    initLogicalDevice();
    initSwapchain();
    initCommandPool();
    initCommandBuffers();
    initDescriptorPool();
    initRenderPass();
    initPipelineLayouts();
    initPipelines();
    initFramebuffer();
    initSyncObjects();
    initImGui();
}

bool Renderer::tick()
{
    bool handleResult = handleEvents();
    if(!handleResult){
        return false;
    }
    render();
    return true;
}

void Renderer::cleanup()
{
    lDevice.waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    lDevice.destroySemaphore(imageAvaliable);
    lDevice.destroySemaphore(renderingFinished);
    lDevice.destroyFence(inflightFence);
    
    for(int i=0;i<imguiFrameBuffers.size();++i){
        lDevice.destroyFramebuffer(imguiFrameBuffers[i]);
        lDevice.destroyFramebuffer(defaultGraphicFrameBuffers[i]);
    }
    lDevice.destroyPipeline(defaultGraphicPipeline);
    lDevice.destroyPipelineLayout(defaultGraphicPipelineLayout);
    lDevice.destroyRenderPass(defaultGraphicRenderPass);
    lDevice.destroyRenderPass(imguiRenderPass);
    lDevice.destroyDescriptorPool(descriptorPool);
    lDevice.destroyCommandPool(graphicCommandPool);
    lDevice.destroyCommandPool(computeCommandPool);
    for(int i=0;i<swapchainImageViews.size();++i){
        lDevice.destroyImageView(swapchainImageViews[i]);
    }
    lDevice.destroySwapchainKHR(swapchain);
    lDevice.destroy();

    vkInstance.destroySurfaceKHR(surface);
    vkInstance.destroyDebugUtilsMessengerEXT(debugMessenger,nullptr,dld);
    vkInstance.destroy();
    SDL_DestroyWindow(sdlWindow);
    
}
bool Renderer::handleEvents(){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT){
            return false;
        }
        else if(event.type == SDL_WINDOWEVENT){
            switch(event.window.event){
                case SDL_WINDOWEVENT_MINIMIZED:
                    windowMinimized = true;
                    break;
                case SDL_WINDOWEVENT_RESTORED:
                    windowMinimized = false;
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    reinitSwapchain();
                    break;
            }
            continue;
        }

        ImGui_ImplSDL2_ProcessEvent(&event);
    }
    return true;
}
void Renderer::render(){

    //imgui render
    auto waitFenceResult = lDevice.waitForFences(inflightFence,true,notimeout);
    lDevice.resetFences(inflightFence);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    {
        if(ImGui::Begin("fps")){
            ImGui::BulletText("fps:%.3f",ImGui::GetIO().Framerate);
            ImGui::SameLine();
            ImGui::BulletText("fps:%.3f",ImGui::GetIO().Framerate);
        }
        ImGui::End();
    }
    ImGui::Render();

    auto [result,frameIdx] = lDevice.acquireNextImageKHR(swapchain,notimeout,imageAvaliable);
    
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    renderingCommandBuffers.begin(beginInfo);
    vk::RenderPassBeginInfo renderpassBeginInfo;
    std::vector<vk::ClearValue> clearValues = {
        vk::ClearValue()
    };
    vk::Rect2D area({0,0},swapchainDetails.extent);
    renderpassBeginInfo.setRenderArea(area);
    renderpassBeginInfo.setRenderPass(defaultGraphicRenderPass);
    renderpassBeginInfo.setClearValues(clearValues);
    renderpassBeginInfo.setFramebuffer(defaultGraphicFrameBuffers[frameIdx]);
    renderingCommandBuffers.beginRenderPass(renderpassBeginInfo,vk::SubpassContents::eInline);
    renderingCommandBuffers.bindPipeline(vk::PipelineBindPoint::eGraphics,defaultGraphicPipeline);
    vk::Viewport viewport;
    viewport.setMinDepth(0);
    viewport.setMaxDepth(0);
    viewport.setWidth(swapchainDetails.extent.width);
    viewport.setHeight(swapchainDetails.extent.height);
    viewport.setX(0.0f);
    viewport.setY(0.0f);
    vk::Rect2D scissor;
    scissor.setExtent(swapchainDetails.extent);
    scissor.setOffset({0,0});
    renderingCommandBuffers.setViewport(0,viewport);
    renderingCommandBuffers.setScissor(0,scissor);
    renderingCommandBuffers.draw(3,1,0,0);
    renderingCommandBuffers.endRenderPass();

    renderpassBeginInfo.setRenderPass(imguiRenderPass);
    renderpassBeginInfo.setFramebuffer(imguiFrameBuffers[frameIdx]);
    renderingCommandBuffers.beginRenderPass(renderpassBeginInfo,vk::SubpassContents::eInline);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),renderingCommandBuffers);
    renderingCommandBuffers.endRenderPass();

    renderingCommandBuffers.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(renderingCommandBuffers);
    submitInfo.setSignalSemaphores(renderingFinished);
    submitInfo.setWaitSemaphores(imageAvaliable);
    std::vector<vk::PipelineStageFlags> waitStages = {
        vk::PipelineStageFlagBits::eTopOfPipe
    };
    submitInfo.setWaitDstStageMask(waitStages);
    graphicQueue.submit(submitInfo,inflightFence);

    vk::PresentInfoKHR presentInfo;
    presentInfo.setImageIndices(frameIdx);
    presentInfo.setSwapchains(swapchain);
    presentInfo.setWaitSemaphores(renderingFinished);

    auto presentResult = presentQueue.presentKHR(presentInfo);


}
void Renderer::initDLD()
{
    dld = vk::DispatchLoaderDynamic(vkInstance,vkGetInstanceProcAddr);
}

void Renderer::initSDL()
{
    sdlWindow = SDL_CreateWindow("jasons'renderer",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,800,800,SDL_WINDOW_VULKAN|SDL_WINDOW_RESIZABLE);
}

void Renderer::initVkInstance()
{
    vk::ApplicationInfo appInfo;
    vk::InstanceCreateInfo createInfo;
    createInfo.setPApplicationInfo(&appInfo);
    std::vector<const char*> layers;
    std::vector<const char*> exts;
    uint32_t layerCount = getInstanceLayers(layers);
    uint32_t extCount = getInstanceExts(exts);
    createInfo.setPEnabledLayerNames(layers);
    createInfo.setPEnabledExtensionNames(exts);
    vk::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo;
    debugMessengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
    |vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
    |vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose);

    debugMessengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    debugMessengerInfo.setPfnUserCallback(debugMessengerCallback);
    createInfo.setPNext(&debugMessengerInfo);
    vkInstance = vk::createInstance(createInfo);
}
void Renderer::initDebugMessenger()
{
    vk::DebugUtilsMessengerCreateInfoEXT debugMessengerInfo;
    debugMessengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError
    |vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
    |vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose);

    debugMessengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
    |vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral
    |vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance);
    debugMessengerInfo.setPfnUserCallback(debugMessengerCallback);
    
    debugMessenger = vkInstance.createDebugUtilsMessengerEXT(debugMessengerInfo,nullptr,dld);
}
void Renderer::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL2_InitForVulkan(sdlWindow);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVkResult;
    initInfo.ColorAttachmentFormat = static_cast<VkFormat>(swapchainDetails.format.format);
    initInfo.DescriptorPool = descriptorPool;
    initInfo.Device = lDevice;
    initInfo.ImageCount = swapchainImages.size();
    initInfo.Instance = vkInstance;
    initInfo.MinImageCount = std::min(swapchainDetails.capabilities.minImageCount+1,swapchainDetails.capabilities.maxImageCount);
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.PhysicalDevice = pDevice;
    initInfo.Queue = graphicQueue;
    initInfo.QueueFamily = queueFamilyIndices.graphicQueueFamily.value();
    initInfo.Subpass = 0;
    ImGui_ImplVulkan_Init(&initInfo,imguiRenderPass);

    vk::CommandBufferAllocateInfo allocateInfo;
    allocateInfo.setCommandBufferCount(1);
    allocateInfo.setCommandPool(graphicCommandPool);
    allocateInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    vk::CommandBuffer commandBuffer = lDevice.allocateCommandBuffers(allocateInfo)[0];
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffer.begin(beginInfo);
    ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
    commandBuffer.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(commandBuffer);
    graphicQueue.submit(submitInfo);
    lDevice.waitIdle();
    lDevice.freeCommandBuffers(graphicCommandPool,commandBuffer);

}
void Renderer::initPipelineLayouts()
{
    {
        vk::PipelineLayoutCreateInfo createInfo;
        defaultGraphicPipelineLayout = lDevice.createPipelineLayout(createInfo);
    }
}
void Renderer::initPipelines()
{
    {
        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.setBlendEnable(false);
        colorBlendAttachment.setColorWriteMask(vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA);
        vk::PipelineColorBlendStateCreateInfo colorBlendState;
        colorBlendState.setAttachments(colorBlendAttachment);
        colorBlendState.setLogicOpEnable(false);

        vk::PipelineDepthStencilStateCreateInfo depthStencilState;
        depthStencilState.setDepthBoundsTestEnable(true);
        depthStencilState.setDepthCompareOp(vk::CompareOp::eLess);
        depthStencilState.setDepthTestEnable(false);
        depthStencilState.setDepthWriteEnable(false);
        depthStencilState.setMaxDepthBounds(1);
        depthStencilState.setMinDepthBounds(0);
        depthStencilState.setStencilTestEnable(false);

        vk::PipelineDynamicStateCreateInfo dynamicStates;
        std::vector<vk::DynamicState> states = {vk::DynamicState::eViewport,vk::DynamicState::eScissor};
        dynamicStates.setDynamicStates(states);

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

        vk::PipelineMultisampleStateCreateInfo multiSampleState;
        multiSampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

        vk::PipelineRasterizationStateCreateInfo rasterizationState;
        rasterizationState.setCullMode(vk::CullModeFlagBits::eBack);
        rasterizationState.setDepthClampEnable(false);
        rasterizationState.setFrontFace(vk::FrontFace::eClockwise);
        rasterizationState.setPolygonMode(vk::PolygonMode::eFill);
        rasterizationState.setLineWidth(1.0f);

        vk::ShaderModule vertShaderModule = createShaderModule("shaders/spv/vertshader.spv");
        vk::ShaderModule fragShaderModule = createShaderModule("shaders/spv/fragshader.spv");
        vk::PipelineShaderStageCreateInfo vertShader;
        vertShader.setModule(vertShaderModule);
        vertShader.setPName("main");
        vertShader.setStage(vk::ShaderStageFlagBits::eVertex);
        vk::PipelineShaderStageCreateInfo fragShader;
        fragShader.setModule(fragShaderModule);
        fragShader.setPName("main");
        fragShader.setStage(vk::ShaderStageFlagBits::eFragment);
        std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertShader,fragShader};

        vk::PipelineTessellationStateCreateInfo tessellationState;
        
        vk::PipelineVertexInputStateCreateInfo vertexInputState;
        
        vk::Viewport viewport;
        viewport.setMinDepth(0);
        viewport.setMaxDepth(0);
        viewport.setWidth(swapchainDetails.extent.width);
        viewport.setHeight(swapchainDetails.extent.height);
        viewport.setX(0.0f);
        viewport.setY(0.0f);
        vk::Rect2D scissor;
        scissor.setExtent(swapchainDetails.extent);
        scissor.setOffset({0,0});
        vk::PipelineViewportStateCreateInfo viewportState;
        viewportState.setViewports(viewport);
        viewportState.setScissors(scissor);
    
        vk::GraphicsPipelineCreateInfo createInfo;
        createInfo.setLayout(defaultGraphicPipelineLayout);
        createInfo.setPColorBlendState(&colorBlendState);
        createInfo.setPDepthStencilState(&depthStencilState);
        createInfo.setPDynamicState(&dynamicStates);
        createInfo.setPInputAssemblyState(&inputAssemblyState);
        createInfo.setPMultisampleState(&multiSampleState);
        createInfo.setPRasterizationState(&rasterizationState);
        createInfo.setStages(stages);
        createInfo.setPTessellationState(&tessellationState);
        createInfo.setPVertexInputState(&vertexInputState);
        createInfo.setPViewportState(&viewportState);
        
        createInfo.setSubpass(0);
        createInfo.setRenderPass(defaultGraphicRenderPass);

        vk::ResultValue<vk::Pipeline> resultValue = lDevice.createGraphicsPipeline(nullptr,createInfo);
        if(resultValue.result!=vk::Result::eSuccess){
            throw std::runtime_error("failed to create defaultGraphicPipeline!");
        }
        defaultGraphicPipeline = resultValue.value;
        lDevice.destroyShaderModule(vertShaderModule);
        lDevice.destroyShaderModule(fragShaderModule);
    }
}
void Renderer::initSurface()
{
    VkSurfaceKHR stagingSurface;
    auto result = SDL_Vulkan_CreateSurface(sdlWindow,vkInstance,&stagingSurface);
    if(result != SDL_TRUE){
        throw std::runtime_error("failed to create window surface!");
    }
    surface = stagingSurface;
}
void Renderer::pickPhysicalDevice()
{
    std::vector<vk::PhysicalDevice> pdevices;
    pdevices = vkInstance.enumeratePhysicalDevices();
    for(auto pdevice:pdevices){
        if(checkPhysicalDevice(pdevice)){
            pDevice = pdevice;
            return;
        }
    }
    throw std::runtime_error("failed to have a suitable physical device!");
}
bool Renderer::checkPhysicalDevice(vk::PhysicalDevice pdevice)
{
    bool result = true;
    result&=pickQueueFamilies(pdevice);
    return result;
}
bool Renderer::pickQueueFamilies(vk::PhysicalDevice pdevice)
{
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = pdevice.getQueueFamilyProperties();
    for(int i=0;i<queueFamilyProperties.size();++i){
        auto queueFamilyProperty = queueFamilyProperties[i];
        if(!queueFamilyIndices.computeQueueFamily.has_value()&&(queueFamilyProperty.queueFlags&vk::QueueFlagBits::eCompute)){
            queueFamilyIndices.computeQueueFamily =  i;
        }
        if(!queueFamilyIndices.graphicQueueFamily.has_value()&&(queueFamilyProperty.queueFlags&vk::QueueFlagBits::eGraphics)){
            queueFamilyIndices.graphicQueueFamily =  i;
        }
        if(!queueFamilyIndices.presentQueueFamily.has_value()&&pdevice.getSurfaceSupportKHR(i,surface)){
            queueFamilyIndices.presentQueueFamily =  i;
        }
    }
    return queueFamilyIndices.complete();
}
uint32_t Renderer::getDeviceLayers(std::vector<const char *> &layers)
{
    layers.resize(0);
    return layers.size();
}
uint32_t Renderer::getDeviceExts(std::vector<const char *> &exts)
{
    exts.resize(0);
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    return exts.size();
}

void Renderer::getDeviceFeatures(vk::PhysicalDeviceFeatures &features)
{
    
}

void Renderer::getSwapchainDetails()
{
    swapchainDetails.capabilities = pDevice.getSurfaceCapabilitiesKHR(surface);
    std::vector<vk::SurfaceFormatKHR> formats = pDevice.getSurfaceFormatsKHR(surface);
    bool formatPicked = false;
    for(auto format:formats){
        if(format.format == vk::Format::eR8G8B8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear){
            swapchainDetails.format = format;
            formatPicked = true;
            break;
        }
    }
    if(!formatPicked){
        swapchainDetails.format = formats[0];
    }
    bool presentModePicked = false;
    std::vector<vk::PresentModeKHR> presentModes = pDevice.getSurfacePresentModesKHR(surface);
    for(auto presentMode:presentModes){
        if(presentMode == vk::PresentModeKHR::eMailbox){
            presentModePicked = true;
            swapchainDetails.presentMode = vk::PresentModeKHR::eMailbox;
        }
    }
    if(!presentModePicked){
        swapchainDetails.presentMode = vk::PresentModeKHR::eFifo;
    }
}

vk::ImageView Renderer::createImageView(vk::Image image,vk::Format format,vk::ImageAspectFlags aspectMask)
{
    vk::ImageViewCreateInfo imageViewInfo;
    imageViewInfo.setImage(image);
    imageViewInfo.setComponents(vk::ComponentMapping{});
    imageViewInfo.setFormat(format);
    vk::ImageSubresourceRange subresoureceRange;
    subresoureceRange.setAspectMask(aspectMask);
    subresoureceRange.setBaseArrayLayer(0);
    subresoureceRange.setBaseMipLevel(0);
    subresoureceRange.setLayerCount(1);
    subresoureceRange.setLevelCount(1);
    imageViewInfo.setSubresourceRange(subresoureceRange);
    imageViewInfo.setViewType(vk::ImageViewType::e2D);
    vk::ImageView imageView =  lDevice.createImageView(imageViewInfo);
    return imageView;
}

vk::ShaderModule Renderer::createShaderModule(const char *path)
{
    std::ifstream ifs;
    ifs.open(path,std::ios::ate|std::ios::binary);
    if(!ifs){
        throw std::runtime_error("failed to load shader file!");
    }
    int size = ifs.tellg();
    ifs.seekg(0);
    std::vector<char> bytes(size);
    ifs.read(bytes.data(),size);
    ifs.close();

    vk::ShaderModuleCreateInfo createInfo;
    createInfo.setPCode(reinterpret_cast<uint32_t*>(bytes.data()));
    createInfo.setCodeSize(size);
    vk::ShaderModule shaderModule = lDevice.createShaderModule(createInfo);
    return shaderModule;
}

void Renderer::initLogicalDevice()
{
    pickPhysicalDevice();

    vk::DeviceCreateInfo deviceInfo;
    std::vector<const char*> exts;
    std::vector<const char*> layers;
    vk::PhysicalDeviceFeatures features;
    uint32_t extCount = getDeviceExts(exts);
    uint32_t layerCount = getDeviceLayers(layers);
    getDeviceFeatures(features);
    deviceInfo.setPEnabledExtensionNames(exts);
    deviceInfo.setPEnabledLayerNames(layers);
    deviceInfo.setPEnabledFeatures(&features);
    float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueInfos;
    std::set<uint32_t> familyIndices{queueFamilyIndices.computeQueueFamily.value(),
    queueFamilyIndices.graphicQueueFamily.value(),
    queueFamilyIndices.presentQueueFamily.value()};
    for(auto indice:familyIndices){
        vk::DeviceQueueCreateInfo queueInfo;
        queueInfo.setQueueCount(1);
        queueInfo.setQueueFamilyIndex(indice);
        queueInfo.setQueuePriorities(queuePriority);
        queueInfos.push_back(queueInfo);
    }
    deviceInfo.setQueueCreateInfos(queueInfos);
    lDevice = pDevice.createDevice(deviceInfo);

    computeQueue = lDevice.getQueue(queueFamilyIndices.computeQueueFamily.value(),0);
    graphicQueue = lDevice.getQueue(queueFamilyIndices.graphicQueueFamily.value(),0);
    presentQueue = lDevice.getQueue(queueFamilyIndices.presentQueueFamily.value(),0);
}
void Renderer::loadGLTF()
{
    const char* fileName = "./cude/Cude.gltf";
    std::string err;
    std::string warn;
    gltfLoader.LoadASCIIFromFile(&gltfModel,&err,&warn,fileName);
    
}
void Renderer::initSwapchain()
{
    getSwapchainDetails();
    auto& details = swapchainDetails;
    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.setClipped(true);
    swapchainInfo.setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque);
    swapchainInfo.setImageArrayLayers(1);
    swapchainInfo.setImageColorSpace(details.format.colorSpace);
    vk::Extent2D extent;
    if(details.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = details.capabilities.currentExtent;
    } 
    else{
        int width, height;
        SDL_GetWindowSizeInPixels(sdlWindow,&width,&height);
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        actualExtent.width = std::clamp(actualExtent.width, details.capabilities.minImageExtent.width, details.capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, details.capabilities.minImageExtent.height, details.capabilities.maxImageExtent.height);
        extent = actualExtent;
    }
    details.extent = extent;
    swapchainInfo.setImageExtent(extent);
    swapchainInfo.setImageFormat(details.format.format);
    
    std::set<uint32_t> stagingIndices = {queueFamilyIndices.computeQueueFamily.value(),
    queueFamilyIndices.graphicQueueFamily.value(),
    queueFamilyIndices.presentQueueFamily.value()};
    std::vector<uint32_t> familyIndices(stagingIndices.begin(),stagingIndices.end());
    if(familyIndices.size() > 1){
        swapchainInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
        swapchainInfo.setQueueFamilyIndices(familyIndices);
    }
    else{
        swapchainInfo.setImageSharingMode(vk::SharingMode::eExclusive);
    }
    swapchainInfo.setSurface(surface);
    swapchainInfo.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity);
    swapchainInfo.setMinImageCount(std::min(details.capabilities.minImageCount + 1,details.capabilities.maxImageCount));
    swapchainInfo.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);
    
    swapchain = lDevice.createSwapchainKHR(swapchainInfo);

    swapchainImages = lDevice.getSwapchainImagesKHR(swapchain);
    swapchainImageViews.resize(swapchainImages.size());
    for(int i=0;i<swapchainImageViews.size();++i){
        swapchainImageViews[i] = createImageView(swapchainImages[i],swapchainDetails.format.format,vk::ImageAspectFlagBits::eColor);
    }
}
void Renderer::reinitSwapchain()
{
    lDevice.waitIdle();
    for(int i=0;i<imguiFrameBuffers.size();++i){
        lDevice.destroyFramebuffer(imguiFrameBuffers[i]);
    }
    for(int i=0;i<swapchainImageViews.size();++i){
        lDevice.destroyImageView(swapchainImageViews[i]);
    }
    lDevice.destroySwapchainKHR(swapchain);
    initSwapchain();
    initFramebuffer();
}
void Renderer::initCommandPool()
{
    {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.setQueueFamilyIndex(queueFamilyIndices.computeQueueFamily.value());
        poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        computeCommandPool = lDevice.createCommandPool(poolInfo);
    }
    {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.setQueueFamilyIndex(queueFamilyIndices.graphicQueueFamily.value());
        poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        graphicCommandPool = lDevice.createCommandPool(poolInfo);
    }
}
void Renderer::initCommandBuffers()
{
    vk::CommandBufferAllocateInfo allocateInfo;
    allocateInfo.setCommandBufferCount(1);
    allocateInfo.setCommandPool(graphicCommandPool);
    allocateInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    renderingCommandBuffers = lDevice.allocateCommandBuffers(allocateInfo)[0];
}
void Renderer::initDescriptorPool()
{
    std::array<vk::DescriptorPoolSize,1> poolSizes = {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,8)
    };
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setMaxSets(8);
    poolInfo.setPoolSizes(poolSizes);
    
    descriptorPool = lDevice.createDescriptorPool(poolInfo);
}
void Renderer::initRenderPass()
{
    {
        //imgui renderpass
        vk::AttachmentDescription colorAttachment;
        colorAttachment.setFormat(swapchainDetails.format.format);
        colorAttachment.setInitialLayout(vk::ImageLayout::eUndefined);
        colorAttachment.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
        colorAttachment.setLoadOp(vk::AttachmentLoadOp::eDontCare);
        colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        colorAttachment.setSamples(vk::SampleCountFlagBits::e1);
        std::vector<vk::AttachmentDescription> attachments = {
            colorAttachment,
        };
        vk::AttachmentReference ref_colorAttachment;
        ref_colorAttachment.setAttachment(0);
        ref_colorAttachment.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::SubpassDescription imguiSubpassInfo;
        imguiSubpassInfo.setColorAttachments(ref_colorAttachment);
        imguiSubpassInfo.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        std::vector<vk::SubpassDescription> subpasses = {
            imguiSubpassInfo
        };
        std::vector<vk::SubpassDependency> subpassDependencies={
            vk::SubpassDependency(VK_SUBPASS_EXTERNAL,0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eNone,vk::AccessFlagBits::eColorAttachmentWrite),
        };

        vk::RenderPassCreateInfo renderpassInfo; 
        renderpassInfo.setAttachments(attachments);
        renderpassInfo.setDependencies(subpassDependencies);
        renderpassInfo.setSubpasses(subpasses);

        imguiRenderPass = lDevice.createRenderPass(renderpassInfo);
    }
    {
        //default renderpass
        vk::AttachmentDescription colorAttachment;
        colorAttachment.setFormat(swapchainDetails.format.format);
        colorAttachment.setInitialLayout(vk::ImageLayout::eUndefined);
        //after that,ui should be draw.
        colorAttachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

        colorAttachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        colorAttachment.setSamples(vk::SampleCountFlagBits::e1);
        std::vector<vk::AttachmentDescription> attachments = {
            colorAttachment,
        };
        vk::AttachmentReference ref_colorAttachment;
        ref_colorAttachment.setAttachment(0);
        ref_colorAttachment.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

        vk::SubpassDescription defaultSubpassInfo;
        defaultSubpassInfo.setColorAttachments(ref_colorAttachment);
        defaultSubpassInfo.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        std::vector<vk::SubpassDescription> subpasses = {
            defaultSubpassInfo
        };

        vk::RenderPassCreateInfo renderpassInfo; 
        renderpassInfo.setAttachments(attachments);
        renderpassInfo.setSubpasses(subpasses);

        defaultGraphicRenderPass = lDevice.createRenderPass(renderpassInfo);
    }
}
void Renderer::initFramebuffer()
{
    imguiFrameBuffers.resize(swapchainImages.size());
    defaultGraphicFrameBuffers.resize(swapchainImages.size());
    for(int i=0;i<imguiFrameBuffers.size();++i){
        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.setAttachments(swapchainImageViews[i]);
        framebufferInfo.setWidth(swapchainDetails.extent.width);
        framebufferInfo.setHeight(swapchainDetails.extent.height);
        framebufferInfo.setLayers(1);
        framebufferInfo.setRenderPass(imguiRenderPass);
        imguiFrameBuffers[i] = lDevice.createFramebuffer(framebufferInfo);
    }
    for(int i=0;i<defaultGraphicFrameBuffers.size();++i){
        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.setAttachments(swapchainImageViews[i]);
        framebufferInfo.setWidth(swapchainDetails.extent.width);
        framebufferInfo.setHeight(swapchainDetails.extent.height);
        framebufferInfo.setLayers(1);
        framebufferInfo.setRenderPass(defaultGraphicRenderPass);
        defaultGraphicFrameBuffers[i] = lDevice.createFramebuffer(framebufferInfo);
    }

}
void Renderer::initSyncObjects()
{
  
    vk::SemaphoreCreateInfo semaphoreInfo;
    imageAvaliable = lDevice.createSemaphore(semaphoreInfo);
    renderingFinished = lDevice.createSemaphore(semaphoreInfo);
    
    vk::FenceCreateInfo fenceInfo;
    fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);
    inflightFence = lDevice.createFence(fenceInfo);
    
}
uint32_t Renderer::getInstanceLayers(std::vector<const char *> &layers)
{
    layers.resize(0);
    layers.push_back("VK_LAYER_KHRONOS_validation");
    return layers.size();
}

uint32_t Renderer::getInstanceExts(std::vector<const char *>& exts)
{
    exts.resize(0);
    uint32_t sdlExtCounts;
    std::vector<const char*> sdlExts;
    SDL_Vulkan_GetInstanceExtensions(sdlWindow,&sdlExtCounts,nullptr);
    sdlExts.resize(sdlExtCounts);
    SDL_Vulkan_GetInstanceExtensions(sdlWindow,&sdlExtCounts,sdlExts.data());
    for(int i=0;i<sdlExtCounts;++i){
        exts.push_back(sdlExts[i]);
    }
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return exts.size();
}

