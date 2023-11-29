#ifndef VKGLTF_H
#define VKGLTF
#include"vulkan/vulkan.hpp"
#include"tiny_gltf.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include"glm/glm.hpp"
#include"glm/gtc/matrix_transform.hpp"
#include"glm/gtc/type_ptr.hpp"

class Renderer;
namespace vkglTF{

struct VkBase{
    vk::PhysicalDevice physicalDevice;
    vk::Device device;

    uint32_t graphicQueueFamily;
    vk::Queue graphicQueue;
    uint32_t computeQueueFamily;
    vk::Queue computeQueue;

    vk::CommandPool commandPool;
    vk::DescriptorPool descriptorPool;
};
struct Vertex{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec2 uv0;
    glm::vec2 uv1;
    int materialID;
    int modelMatID;
    static vk::VertexInputBindingDescription getBindingDescription(){
        vk::VertexInputBindingDescription binding;
        binding.setBinding(0);
        binding.setInputRate(vk::VertexInputRate::eVertex);
        binding.setStride(sizeof(Vertex));
        return binding;
    }
    static std::array<vk::VertexInputAttributeDescription,7> getAttributesDescription(){
        std::array<vk::VertexInputAttributeDescription,7> attributes;
        attributes[0].setBinding(0);
        attributes[0].setFormat(vk::Format::eR32G32B32Sfloat);
        attributes[0].setLocation(0);
        attributes[0].setOffset(offsetof(Vertex,position));

        attributes[1].setBinding(0);
        attributes[1].setFormat(vk::Format::eR32G32B32Sfloat);
        attributes[1].setLocation(1);
        attributes[1].setOffset(offsetof(Vertex,normal));

        attributes[2].setBinding(0);
        attributes[2].setFormat(vk::Format::eR32G32B32Sfloat);
        attributes[2].setLocation(2);
        attributes[2].setOffset(offsetof(Vertex,tangent));

        attributes[3].setBinding(0);
        attributes[3].setFormat(vk::Format::eR32G32Sfloat);
        attributes[3].setLocation(3);
        attributes[3].setOffset(offsetof(Vertex,uv0));

        attributes[4].setBinding(0);
        attributes[4].setFormat(vk::Format::eR32G32Sfloat);
        attributes[4].setLocation(4);
        attributes[4].setOffset(offsetof(Vertex,uv1));

        attributes[5].setBinding(0);
        attributes[5].setFormat(vk::Format::eR32Sint);
        attributes[5].setLocation(5);
        attributes[5].setOffset(offsetof(Vertex,materialID));

        attributes[6].setBinding(0);
        attributes[6].setFormat(vk::Format::eR32Sint);
        attributes[6].setLocation(6);
        attributes[6].setOffset(offsetof(Vertex,modelMatID));

        return attributes;
    }
};
struct Texture{
    //vk stuff
    int index;
    vk::Image textureImage;
    vk::ImageView textureImageView;
    vk::DeviceMemory imageMemory;
    vk::Sampler imageSampler;
    vk::DescriptorImageInfo desriptorImageInfo;

    int width;
    int height;
};
struct MaterialProperties{
    alignas(16) glm::vec4 basColorFactor={};
    alignas(4) float metallicFactor=0;
    alignas(4) float roughnessFactor=0;
    alignas(4) float normalScale=0;
    alignas(4) float occlusionStength=0;
    alignas(16) glm::vec3 emissiveFactor={};
    alignas(4) int texCoord_baseColor=-1;
    alignas(4) int texCoord_metallicRoughness=-1;
    alignas(4) int texCoord_normal=-1;
    alignas(4) int texCoord_occlusion=-1;
    alignas(4) int texCoord_emissive=-1;
};
struct Material{ 
    int index;
    Texture* baseColorTexture;
    Texture* metallicRoughnessTexture;
    Texture* normalTexture;
    Texture* occlusionTexture;
    Texture* emissiveTexture;

    MaterialProperties properties;

    vk::Buffer uniformMaterialBuffer;
    vk::DeviceMemory uniformMaterialBufferMemory;
    vk::DescriptorBufferInfo descriptorBufferInfo;

    vk::DescriptorSet desriptorSet;
};
struct Primitive{
    int vertexStart = 0;
    int verexCount = 0;
    int indexStart = 0;
    int indexCount = 0;

    bool useIndex = false;

    Material* material=nullptr;
};
struct Mesh{
    struct Node* parent=nullptr;
    std::vector<Primitive*> primitives;
    ~Mesh();
};
struct Node{
    Node* parent=nullptr;
    std::vector<Node*> children;
    Mesh* mesh=nullptr;

    glm::vec3 translation={};
    glm::quat rotation={};
    glm::vec3 scale={1,1,1};

    glm::mat4 local_transform;
    glm::mat4 global_transform;

    ~Node();
};
struct ModelMatrix{
    alignas(16) glm::mat4 model;
};
class Scene{
public:
    Scene(Renderer* renderer);
    ~Scene();
    bool loaded = false;
    void loadFile(const char* path);
    void cleanup();
private:
    Texture* loadTexture(tinygltf::Texture& glTFtexture,int index);
    Material* loadMaterial(tinygltf::Material& glTFmaterial,int index);

    Node* loadNode(tinygltf::Node& glTFnode,Node* parent);
    Mesh* loadMesh(tinygltf::Mesh& glTFmesh,Node* parent);
    Primitive* loadPrimitive(tinygltf::Primitive& glTFprimitivem,int modelMatID);
public:
    std::vector<Texture*> textures;
    std::vector<Material*> materials;
    std::vector<Node*> rtNodes;
    std::vector<uint32_t> indexs;
    std::vector<Vertex> vertices;
    std::vector<ModelMatrix> modelMats;
private:
    tinygltf::Model glTFmodel;
private:
    Renderer* renderer;
public:
    vk::DescriptorSetLayout materialDescriptorSetLayout;
    vk::DescriptorSet materialDescriptorSet;

    vk::DescriptorSetLayout modelMatsDescriptorSetLayout;
    vk::DescriptorSet modelMatsDescriptorSet;
    vk::Buffer modelMatsBuffer;
    vk::DeviceMemory modelMatsBufferMemory;

    vk::Buffer vertexBuffer;
    vk::DeviceMemory vertexBufferMemory;

    vk::Buffer indexBuffer;
    vk::DeviceMemory indexBufferMemory;
};
}
#endif