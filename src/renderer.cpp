#include "renderer.h"
#include"vkglTF.h"

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
    initCommandPool();
    initCommandBuffers();
    initDescriptorPool();
    initglTFScene();
    initSwapchain();
    initDepthResources();
    initCamera();
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
       for(int i=0;i<swapchainImageViews.size();++i){
        lDevice.destroyImageView(swapchainImageViews[i]);
    }
    lDevice.destroyImageView(depthImageView);
    lDevice.destroyImage(depthImage);
    lDevice.freeMemory(depthImageMemory);
    lDevice.destroySwapchainKHR(swapchain);
    delete glTFScene;
    lDevice.destroyDescriptorPool(descriptorPool);
    lDevice.destroyCommandPool(graphicCommandPool);
    lDevice.destroyCommandPool(computeCommandPool);
    lDevice.destroy();

    vkInstance.destroySurfaceKHR(surface);
    vkInstance.destroyDebugUtilsMessengerEXT(debugMessenger,nullptr,dld);
    vkInstance.destroy();
    SDL_DestroyWindow(sdlWindow);
    
}
bool Renderer::handleEvents(){
    SDL_Event event;

    while(SDL_PollEvent(&event)){
        bool handled = false;
        if(event.type == SDL_QUIT){
            handled = true;
            return false;
        }
        else if(event.type == SDL_WINDOWEVENT){
            switch(event.window.event){
                case SDL_WINDOWEVENT_MINIMIZED:
                    handled = true;
                    windowMinimized = true;
                    break;
                case SDL_WINDOWEVENT_RESTORED:
                    windowMinimized = false;
                    handled = true;
                    break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                    handled = true;
                    reinitSwapchain();
                    break;
                case SDL_WINDOWEVENT_LEAVE:
                    handled = true;
                    moveDown = moveLeft = moveRight = moveUp = false;
                    rightButtonDown = false;
                    break;
            }
        }
        else if(event.type == SDL_MOUSEBUTTONDOWN){
            switch(event.button.button){
                case SDL_BUTTON_RIGHT:
                    handled = true;
                    rightButtonDown = true;
                    break;
            }
        }
        else if(event.type == SDL_MOUSEBUTTONUP){
            switch(event.button.button){
                case SDL_BUTTON_RIGHT:
                    handled = true;
                    rightButtonDown = false;
                    break;
            }
        }
        else if(event.type == SDL_KEYDOWN){
            switch(event.key.keysym.sym){
                case SDLK_w:
                    handled = true;
                    moveUp = true;
                    break;
                case SDLK_a:
                    handled = true;
                    moveLeft = true;
                    break;
                case SDLK_s:
                    handled = true;
                    moveDown = true;
                    break;
                case SDLK_d:
                    handled = true;
                    moveRight = true;
                    break;
            }
        }
        else if(event.type == SDL_KEYUP){
            switch(event.key.keysym.sym){
                case SDLK_w:
                    handled = true;
                    moveUp = false;
                    break;
                case SDLK_a:
                    handled = true;
                    moveLeft = false;
                    break;
                case SDLK_s:
                    handled = true;
                    moveDown = false;
                    break;
                case SDLK_d:
                    handled = true;
                    moveRight = false;
                    break;
            }
        }
        else if(event.type == SDL_MOUSEWHEEL){
            handled = true;
            camera.cameraPosition += wheelSpeedScale*event.wheel.y*moveSpeed*camera.viewDirection;
        }
        else if(event.type == SDL_MOUSEMOTION){
            if(rightButtonDown){
                handled = true;
                float xrel = event.motion.xrel;
                float yrel = event.motion.yrel;
                glm::mat4 rotation = glm::mat4(1.0f);
                glm::vec3 x = glm::normalize(glm::cross(camera.viewDirection,glm::vec3(0,1,0)));
                rotation = glm::rotate(rotation,-glm::radians(rotationSpeed*yrel),x);
                rotation = glm::rotate(rotation,-glm::radians(rotationSpeed*xrel),glm::vec3(0,1,0));
                camera.viewDirection = rotation*glm::vec4(camera.viewDirection,0);
            }
        }
        if(!handled){
            ImGui_ImplSDL2_ProcessEvent(&event);
        }
        
    }

    float nowSDLtime = SDL_GetTicks()/1000.0f;
    float deltaTime = nowSDLtime - lastSDLtime;
    lastSDLtime = nowSDLtime;

    if(moveLeft){
        camera.cameraPosition -=  moveSpeed*deltaTime*glm::normalize(glm::cross(camera.viewDirection,glm::vec3(0,1,0)));
    }
    if(moveRight){
        camera.cameraPosition +=  moveSpeed*deltaTime*glm::normalize(glm::cross(camera.viewDirection,glm::vec3(0,1,0)));
    }
    if(moveUp){
        camera.cameraPosition +=  moveSpeed*deltaTime*glm::vec3(0,1,0);
    }
    if(moveDown){
        camera.cameraPosition +=  moveSpeed*deltaTime*glm::vec3(0,-1,0);
    }
    camera.viewMat = glm::lookAt(camera.cameraPosition,camera.cameraPosition+camera.viewDirection,glm::vec3{0,1,0});

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
        }
        ImGui::End();
    }
    ImGui::Render();

    auto [result,frameIdx] = lDevice.acquireNextImageKHR(swapchain,notimeout,imageAvaliable);
    
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    renderingCommandBuffers.begin(beginInfo);
    vk::RenderPassBeginInfo renderpassBeginInfo;

    vk::ClearValue depthStencilClearValue;
    depthStencilClearValue.setDepthStencil(vk::ClearDepthStencilValue{1,0});
    std::vector<vk::ClearValue> clearValues = {
        vk::ClearValue(),depthStencilClearValue
    };

    vk::Rect2D area({0,0},swapchainDetails.extent);
    renderpassBeginInfo.setRenderArea(area);
    renderpassBeginInfo.setRenderPass(defaultGraphicRenderPass);
    renderpassBeginInfo.setClearValues(clearValues);
    renderpassBeginInfo.setFramebuffer(defaultGraphicFrameBuffers[frameIdx]);
    renderingCommandBuffers.beginRenderPass(renderpassBeginInfo,vk::SubpassContents::eInline);
    renderingCommandBuffers.bindPipeline(vk::PipelineBindPoint::eGraphics,defaultGraphicPipeline);
    
    renderingCommandBuffers.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,defaultGraphicPipelineLayout,0,{
        glTFScene->modelMatsDescriptorSet,
        glTFScene->materialDescriptorSet
    },{});

    renderingCommandBuffers.bindVertexBuffers(0,{glTFScene->vertexBuffer},{0});
    renderingCommandBuffers.bindIndexBuffer(glTFScene->indexBuffer,0,vk::IndexType::eUint32);
    renderingCommandBuffers.pushConstants<CameraDetails>(defaultGraphicPipelineLayout,vk::ShaderStageFlagBits::eVertex,0,camera);

    vk::Viewport viewport;
    viewport.setMinDepth(0.0f);
    viewport.setMaxDepth(1.0f);
    viewport.setWidth(swapchainDetails.extent.width);
    viewport.setHeight(swapchainDetails.extent.height);
    viewport.setX(0.0f);
    viewport.setY(0.0f);
    vk::Rect2D scissor;
    scissor.setExtent(swapchainDetails.extent);
    scissor.setOffset({0,0});
    renderingCommandBuffers.setViewport(0,viewport);
    renderingCommandBuffers.setScissor(0,scissor);
    renderingCommandBuffers.drawIndexed(glTFScene->indexs.size(),1,0,0,0);
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
    appInfo.setApiVersion(VK_MAKE_API_VERSION(0,1,3,0));
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
        std::vector<vk::DescriptorSetLayout> setLayouts = {
            glTFScene->modelMatsDescriptorSetLayout,
            glTFScene->materialDescriptorSetLayout,
        };
        vk::PushConstantRange range;
        range.setOffset(0);
        range.setSize(sizeof(CameraDetails));
        range.setStageFlags(vk::ShaderStageFlagBits::eVertex);
        vk::PipelineLayoutCreateInfo createInfo;
        createInfo.setSetLayouts(setLayouts);
        createInfo.setPushConstantRanges(range);
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
        depthStencilState.setDepthCompareOp(vk::CompareOp::eLess);
        depthStencilState.setDepthTestEnable(true);
        depthStencilState.setDepthWriteEnable(true);
        depthStencilState.setMaxDepthBounds(1);
        depthStencilState.setMinDepthBounds(0);

        vk::PipelineDynamicStateCreateInfo dynamicStates;
        std::vector<vk::DynamicState> states = {vk::DynamicState::eViewport,vk::DynamicState::eScissor};
        dynamicStates.setDynamicStates(states);

        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
        inputAssemblyState.setTopology(vk::PrimitiveTopology::eTriangleList);

        vk::PipelineMultisampleStateCreateInfo multiSampleState;
        multiSampleState.setRasterizationSamples(vk::SampleCountFlagBits::e1);

        vk::PipelineRasterizationStateCreateInfo rasterizationState;
        rasterizationState.setCullMode(vk::CullModeFlagBits::eNone);
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
        auto attributeDescriptions = vkglTF::Vertex::getAttributesDescription();
        vertexInputState.setVertexAttributeDescriptions(attributeDescriptions);
        auto bindingDescription = vkglTF::Vertex::getBindingDescription();
        vertexInputState.setVertexBindingDescriptions(bindingDescription);
        
        vk::Viewport viewport;
        viewport.setMinDepth(0);
        viewport.setMaxDepth(1);
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
    std::vector<const char*> exts;
    uint32_t extCount = getDeviceExts(exts);
    
    std::vector<vk::ExtensionProperties> pdeviceExts = pdevice.enumerateDeviceExtensionProperties();
    for(int i=0;i<extCount;++i){
        bool found = false;
        for(auto& pdeviceExt:pdeviceExts){
            if(std::strcmp(exts[i],pdeviceExt.extensionName)==0){
                found = true;
                break;
            }
        }
        if(!found){
            return false;
        }
    }

    if(!pickQueueFamilies(pdevice)){
        return false;
    }
    return true;
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

vk::CommandBuffer Renderer::startOneShotCommandBuffer(vk::CommandPool cp)
{
    vk::CommandBufferAllocateInfo commandBufferInfo;
    commandBufferInfo.setCommandBufferCount(1);
    commandBufferInfo.setCommandPool(graphicCommandPool);
    commandBufferInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    vk::CommandBuffer cb = lDevice.allocateCommandBuffers(commandBufferInfo)[0];
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cb.begin(beginInfo);
    return cb;
}

void Renderer::finishOneShotCommandBuffer(vk::CommandPool cp,vk::CommandBuffer cb,vk::Queue q)
{
    cb.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(cb);
    q.submit(submitInfo);
    lDevice.waitIdle();
    lDevice.freeCommandBuffers(cp,cb);
}

int Renderer::getSuitableMemoryTypeIndex(uint32_t memoryTypeBits,vk::MemoryPropertyFlags props)
{
    vk::PhysicalDeviceMemoryProperties memoryProperties = pDevice.getMemoryProperties();
    for(int i=0;i<memoryProperties.memoryTypeCount;++i){
        vk::MemoryType type = memoryProperties.memoryTypes[i];
        if(type.propertyFlags&props){
            if((1<<i)&memoryTypeBits){
                return i;
            }
        }
    }
    throw std::runtime_error("failed to find a suitable memory type!");
    return -1;
}

void Renderer::createImage(vk::Image& image,vk::DeviceMemory& imageMemory,vk::Extent2D extent,vk::Format format,
                           vk::ImageUsageFlags usages,vk::MemoryPropertyFlags memoryProps)
{
    vk::ImageCreateInfo createInfo;
    createInfo.setArrayLayers(1);
    createInfo.setExtent(vk::Extent3D{extent,1});
    createInfo.setFormat(format);
    createInfo.setImageType(vk::ImageType::e2D);
    createInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    createInfo.setMipLevels(1);
    createInfo.setQueueFamilyIndices(queueFamilyIndices.graphicQueueFamily.value());
    createInfo.setSamples(vk::SampleCountFlagBits::e1);
    createInfo.setSharingMode(vk::SharingMode::eExclusive);
    createInfo.setTiling(vk::ImageTiling::eOptimal);
    createInfo.setUsage(usages);
    image = lDevice.createImage(createInfo);

    vk::MemoryRequirements requirements = lDevice.getImageMemoryRequirements(image);
    int memoryTypeIndex = getSuitableMemoryTypeIndex(requirements.memoryTypeBits,memoryProps);
    vk::MemoryAllocateInfo allocateInfo;
    allocateInfo.setAllocationSize(requirements.size);
    allocateInfo.setMemoryTypeIndex(memoryTypeIndex);
    imageMemory = lDevice.allocateMemory(allocateInfo);
    lDevice.bindImageMemory(image,imageMemory,0);
}

void Renderer::createBuffer(vk::Buffer& buffer,vk::DeviceMemory& bufferMemory,int size,vk::BufferUsageFlags usages,vk::MemoryPropertyFlags memoryProps)
{
    vk::BufferCreateInfo bufferInfo;
    if(queueFamilyIndices.computeQueueFamily.value() == queueFamilyIndices.graphicQueueFamily.value()){
        bufferInfo.setQueueFamilyIndices(queueFamilyIndices.computeQueueFamily.value());
        bufferInfo.setSharingMode(vk::SharingMode::eExclusive);
    }
    else{
        bufferInfo.setSharingMode(vk::SharingMode::eConcurrent);
        std::vector<uint32_t> indices = {queueFamilyIndices.computeQueueFamily.value(),queueFamilyIndices.graphicQueueFamily.value()};
        bufferInfo.setQueueFamilyIndices(indices);
    }
    bufferInfo.setUsage(usages);
    bufferInfo.setSize(size);
    buffer = lDevice.createBuffer(bufferInfo);
    vk::MemoryRequirements requirements = lDevice.getBufferMemoryRequirements(buffer);
    int memoryTypeIndex = getSuitableMemoryTypeIndex(requirements.memoryTypeBits,memoryProps);
    vk::MemoryAllocateInfo allocateInfo;
    allocateInfo.setAllocationSize(requirements.size);
    allocateInfo.setMemoryTypeIndex(memoryTypeIndex);
    bufferMemory = lDevice.allocateMemory(allocateInfo);
    lDevice.bindBufferMemory(buffer,bufferMemory,0);
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
    vk::PhysicalDeviceDescriptorIndexingFeatures diFeatures;
    diFeatures.setDescriptorBindingPartiallyBound(true);
    deviceInfo.setPNext(&diFeatures);
    lDevice = pDevice.createDevice(deviceInfo);

    computeQueue = lDevice.getQueue(queueFamilyIndices.computeQueueFamily.value(),0);
    graphicQueue = lDevice.getQueue(queueFamilyIndices.graphicQueueFamily.value(),0);
    presentQueue = lDevice.getQueue(queueFamilyIndices.presentQueueFamily.value(),0);
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
void Renderer::initCamera()
{
    //sponza
    // camera.cameraPosition = {10,3,0};
    // camera.viewDirection = glm::normalize(glm::vec3(-10,-1,0));
    camera.cameraPosition = {3,3,3};
    //damgedHelmet
    camera.viewDirection = glm::normalize(glm::vec3(-1,-1,-1));
    camera.viewMat = glm::lookAt(camera.cameraPosition,camera.cameraPosition+camera.viewDirection,glm::vec3{0,1,0});
    camera.projectionMat = glm::perspective(glm::radians(45.0f),1.0f*swapchainDetails.extent.width/swapchainDetails.extent.height,0.01f,1000.0f);
    camera.projectionMat[1][1] *= -1;
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
    lDevice.destroyImageView(depthImageView);
    lDevice.destroyImage(depthImage);
    lDevice.freeMemory(depthImageMemory);
    lDevice.destroySwapchainKHR(swapchain);
    initSwapchain();
    initDepthResources();
    initFramebuffer();
}
void Renderer::initDepthResources()
{
    createImage(depthImage,depthImageMemory,swapchainDetails.extent,vk::Format::eD32SfloatS8Uint,
    vk::ImageUsageFlagBits::eDepthStencilAttachment,vk::MemoryPropertyFlagBits::eDeviceLocal);
    depthImageView = createImageView(depthImage,vk::Format::eD32SfloatS8Uint,vk::ImageAspectFlagBits::eDepth);
    vk::CommandBuffer cb = startOneShotCommandBuffer(graphicCommandPool);
    vk::ImageMemoryBarrier imageBarrier;
    imageBarrier.setImage(depthImage);
    imageBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.setSrcAccessMask(vk::AccessFlags(0));
    imageBarrier.setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead|vk::AccessFlagBits::eDepthStencilAttachmentWrite);
    imageBarrier.setOldLayout(vk::ImageLayout::eUndefined);
    imageBarrier.setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eEarlyFragmentTests,vk::DependencyFlags(0),{},{},imageBarrier);
    finishOneShotCommandBuffer(graphicCommandPool,cb,graphicQueue);
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
void Renderer::initglTFScene()
{
    vkglTF::VkBase vkBase;
    vkBase.commandPool = graphicCommandPool;
    vkBase.device = lDevice;
    vkBase.physicalDevice = pDevice;
    vkBase.graphicQueue = graphicQueue;
    vkBase.graphicQueueFamily = queueFamilyIndices.graphicQueueFamily.value();
    glTFScene = new vkglTF::Scene(this);
    glTFScene->loadFile("assets/damagedHelmet/DamagedHelmet.gltf");
}
void Renderer::initDescriptorPool()
{
    std::array<vk::DescriptorPoolSize,3> poolSizes = {
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler,1024),
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer,1024),
        vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer,8),
    };
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setMaxSets(16);
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
        colorAttachment.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

        colorAttachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        colorAttachment.setSamples(vk::SampleCountFlagBits::e1);

        vk::AttachmentDescription depthAttachment;
        depthAttachment.setFormat(vk::Format::eD32SfloatS8Uint);
        depthAttachment.setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        depthAttachment.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        depthAttachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        depthAttachment.setStoreOp(vk::AttachmentStoreOp::eDontCare);
        depthAttachment.setSamples(vk::SampleCountFlagBits::e1);

        std::vector<vk::AttachmentDescription> attachments = {
            colorAttachment,depthAttachment
        };
        vk::AttachmentReference ref_colorAttachment;
        ref_colorAttachment.setAttachment(0);
        ref_colorAttachment.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference ref_depthAttachment;
        ref_depthAttachment.setAttachment(1);
        ref_depthAttachment.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::SubpassDescription defaultSubpassInfo;
        defaultSubpassInfo.setColorAttachments(ref_colorAttachment);
        defaultSubpassInfo.setPDepthStencilAttachment(&ref_depthAttachment);
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
        std::array<vk::ImageView,2> attachments = {swapchainImageViews[i],depthImageView};
        framebufferInfo.setAttachments(attachments);
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

