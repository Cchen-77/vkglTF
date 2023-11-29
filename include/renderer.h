#ifndef RENDERER_H
#define RENDERER_H
#include"imgui.h"
#include"imgui_impl_vulkan.h"
#include"imgui_impl_sdl2.h"

#include"vulkan/vulkan.hpp"

#define SDL_MAIN_HANDLED
#include"SDL.h"
#include"SDL_vulkan.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include"glm/glm.hpp"
#include"glm/gtc/matrix_transform.hpp"
#include"glm/gtc/type_ptr.hpp"


#include<vector>
#include<optional>

namespace vkglTF{
    class Scene;
}
struct CameraDetails{
    alignas(16) glm::vec3 cameraPosition;
    alignas(16) glm::vec3 viewDirection;
    alignas(16) glm::mat4 viewMat;
    alignas(16) glm::mat4 projectionMat;
};
struct QueueFamiliyIndices{
    std::optional<uint32_t> graphicQueueFamily;
    std::optional<uint32_t> computeQueueFamily;
    std::optional<uint32_t> presentQueueFamily;
    bool complete(){
        return graphicQueueFamily.has_value()&&computeQueueFamily.has_value()&&presentQueueFamily.has_value();
    }
};
struct SwapchainDetails{
    vk::SurfaceFormatKHR format;
    vk::SurfaceCapabilitiesKHR capabilities;
    vk::PresentModeKHR presentMode;
    vk::Extent2D extent;
};
class Renderer{
    friend class vkglTF::Scene;
public:
    Renderer();
    ~Renderer();
public:
    void init();
    bool tick();
    void cleanup();
public:
    bool handleEvents();
    void render();
private:
    //basic stuff
    void initSDL();
    void initVkInstance();
    void initDLD();
    void initDebugMessenger();
    void initSurface();
    void initLogicalDevice();
    void initCommandPool();
    void initCommandBuffers();
    
    void initglTFScene();
    
    //rendering stuff
    void initSwapchain();
    void initCamera();
    void reinitSwapchain();
    void initDepthResources();
    void initDescriptorPool();
    void initRenderPass();
    void initFramebuffer();
    void initSyncObjects();
    void initImGui();
    void initPipelineLayouts();
    void initPipelines();
private:
    static void checkVkResult(VkResult result);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
    void*                                            pUserData);
private:
    uint32_t getInstanceLayers(std::vector<const char*>& layers);
    uint32_t getInstanceExts(std::vector<const char*>& exts);
    void pickPhysicalDevice();
    bool checkPhysicalDevice(vk::PhysicalDevice pdevice);
    bool pickQueueFamilies(vk::PhysicalDevice pdevice);
    uint32_t getDeviceLayers(std::vector<const char*>& layers);
    uint32_t getDeviceExts(std::vector<const char*>& exts);
    void getDeviceFeatures(vk::PhysicalDeviceFeatures& features);
    void getSwapchainDetails();
    vk::ImageView createImageView(vk::Image image,vk::Format format,vk::ImageAspectFlags aspectMask);
    int getSuitableMemoryTypeIndex(uint32_t memoryTypeBits,vk::MemoryPropertyFlags props);
    void createImage(vk::Image& image,vk::DeviceMemory& imageMemory,vk::Extent2D extent,vk::Format format,
                    vk::ImageUsageFlags usages,vk::MemoryPropertyFlags memoryProps);
    void createBuffer(vk::Buffer& buffer,vk::DeviceMemory& bufferMemory,int size,vk::BufferUsageFlags usages,vk::MemoryPropertyFlags memoryProps);
    vk::ShaderModule createShaderModule(const char* path);

    vk::CommandBuffer startOneShotCommandBuffer(vk::CommandPool cp);
    void finishOneShotCommandBuffer(vk::CommandPool cp,vk::CommandBuffer cb,vk::Queue q);
private:
    SDL_Window* sdlWindow;
    float lastSDLtime = 0.0f;
    bool windowMinimized = false;
    bool moveLeft=false;
    bool moveRight=false;
    bool moveUp=false;
    bool moveDown=false;
    bool rightButtonDown=false;
    float wheelSpeedScale = 0.3;
    float moveSpeed=2.0;
    float rotationSpeed=0.1;

private:
    vkglTF::Scene* glTFScene;
private:
    vk::DispatchLoaderDynamic dld;
    vk::Instance vkInstance;
    vk::SurfaceKHR surface;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice pDevice;
    vk::Device lDevice;
    QueueFamiliyIndices queueFamilyIndices;
    vk::Queue graphicQueue;
    vk::Queue computeQueue;
    vk::Queue presentQueue;
    vk::SwapchainKHR swapchain;
    SwapchainDetails swapchainDetails;
    std::vector<vk::Image> swapchainImages;
    std::vector<vk::ImageView> swapchainImageViews;
    vk::CommandPool graphicCommandPool;
    vk::CommandPool computeCommandPool;
    vk::DescriptorPool descriptorPool;

    vk::CommandBuffer renderingCommandBuffers;

    vk::RenderPass imguiRenderPass;

    vk::PipelineLayout defaultGraphicPipelineLayout;
    vk::Pipeline defaultGraphicPipeline;
    vk::RenderPass defaultGraphicRenderPass;

    std::vector<vk::Framebuffer> imguiFrameBuffers;
    std::vector<vk::Framebuffer> defaultGraphicFrameBuffers;

    vk::Semaphore imageAvaliable;
    vk::Semaphore renderingFinished;
    vk::Fence inflightFence;

    vk::Image depthImage;
    vk::DeviceMemory depthImageMemory;
    vk::ImageView depthImageView;
private:
    int curFrame = 0;
    int maxInFlightFrame = 2;

    CameraDetails camera;
};
#endif