#include "vkglTF.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include"tiny_gltf.h"

#include"renderer.h"

#include<iostream>
#include<string>

#define MAX_MATERIAL_COUNT 128
namespace vkglTF{

Scene::Scene(Renderer* renderer):renderer(renderer)
{
    
}

Scene::~Scene()
{
    for(int i=0;i<materials.size();++i){
        renderer->lDevice.destroyBuffer(materials[i]->uniformMaterialBuffer);
        renderer->lDevice.freeMemory(materials[i]->uniformMaterialBufferMemory);
        delete materials[i];
    }
    for(int i=0;i<textures.size();++i){
        renderer->lDevice.destroyImageView(textures[i]->textureImageView);
        renderer->lDevice.destroyImage(textures[i]->textureImage);
        renderer->lDevice.destroySampler(textures[i]->imageSampler);
        renderer->lDevice.freeMemory(textures[i]->imageMemory);
        delete textures[i];
    }
    for(int i=0;i<rtNodes.size();++i){
        delete rtNodes[i];
    }
    cleanup();
}
void Scene::cleanup(){
    if(loaded){
        renderer->lDevice.destroyBuffer(vertexBuffer);
        renderer->lDevice.freeMemory(vertexBufferMemory);
        renderer->lDevice.destroyBuffer(indexBuffer);
        renderer->lDevice.freeMemory(indexBufferMemory);
        renderer->lDevice.destroyBuffer(modelMatsBuffer);
        renderer->lDevice.freeMemory(modelMatsBufferMemory);
        renderer->lDevice.destroyDescriptorSetLayout(materialDescriptorSetLayout);
        renderer->lDevice.destroyDescriptorSetLayout(modelMatsDescriptorSetLayout);
    }
}
void Scene::loadFile(const char *path)
{
    if(loaded){
        throw std::runtime_error("scene is already loaded!");
    }
    loaded = true;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    bool result = loader.LoadASCIIFromFile(&glTFmodel,&err,&warn,path);
    if(!result){
        throw std::runtime_error("failed to load glTF!");
    }
    //a descriptorSet describe all materials:
    {
        std::array<vk::DescriptorSetLayoutBinding,6> bindings;
        bindings[0].setBinding(0);
        bindings[0].setDescriptorCount(MAX_MATERIAL_COUNT);
        bindings[0].setDescriptorType(vk::DescriptorType::eUniformBuffer);
        bindings[0].setStageFlags(vk::ShaderStageFlagBits::eFragment);

        for(int i=1;i<6;++i){
            bindings[i].setBinding(i);
            bindings[i].setDescriptorCount(MAX_MATERIAL_COUNT);
            bindings[i].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            bindings[i].setStageFlags(vk::ShaderStageFlagBits::eFragment);
        }
        std::array<vk::DescriptorBindingFlags,6> bindingFlags;
        for(int i=0;i<6;++i){
            bindingFlags[i] = vk::DescriptorBindingFlagBits::ePartiallyBound;
        }
        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo;
        bindingFlagsCreateInfo.setBindingFlags(bindingFlags);
        vk::DescriptorSetLayoutCreateInfo createInfo;
        createInfo.setBindings(bindings);
        createInfo.setPNext(&bindingFlagsCreateInfo);
        
        materialDescriptorSetLayout = renderer->lDevice.createDescriptorSetLayout(createInfo);
        vk::DescriptorSetAllocateInfo allocateInfo;
        allocateInfo.setDescriptorPool(renderer->descriptorPool);
        allocateInfo.setDescriptorSetCount(1);
        allocateInfo.setSetLayouts(materialDescriptorSetLayout);
        materialDescriptorSet = renderer->lDevice.allocateDescriptorSets(allocateInfo)[0];
    }

    for(int i=0;i<glTFmodel.textures.size();++i){
        Texture* texture = loadTexture(glTFmodel.textures[i],i);
        textures.push_back(texture);
    }
    for(int i=0;i<glTFmodel.materials.size();++i){
        Material* material = loadMaterial(glTFmodel.materials[i],i);
        materials.push_back(material);
    }
    for(int node:glTFmodel.scenes[glTFmodel.defaultScene].nodes){
        Node* rtNode = loadNode(glTFmodel.nodes[node],nullptr);
        rtNodes.push_back(rtNode);
    }
    //build modelMat descriptorSet
    {
        vk::DescriptorSetLayoutBinding binding;
        binding.setBinding(0);
        binding.setDescriptorCount(1);
        binding.setDescriptorType(vk::DescriptorType::eStorageBuffer);
        binding.setStageFlags(vk::ShaderStageFlagBits::eVertex);
        vk::DescriptorSetLayoutCreateInfo createInfo;
        createInfo.setBindings(binding);
        modelMatsDescriptorSetLayout = renderer->lDevice.createDescriptorSetLayout(createInfo);
        vk::DescriptorSetAllocateInfo allocateInfo;
        allocateInfo.setDescriptorPool(renderer->descriptorPool);
        allocateInfo.setDescriptorSetCount(1);
        allocateInfo.setSetLayouts(modelMatsDescriptorSetLayout);
        modelMatsDescriptorSet = renderer->lDevice.allocateDescriptorSets(allocateInfo)[0];
        int size = modelMats.size()*sizeof(ModelMatrix);
        renderer->createBuffer(modelMatsBuffer,modelMatsBufferMemory,size,
        vk::BufferUsageFlagBits::eStorageBuffer|vk::BufferUsageFlagBits::eTransferDst,vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        renderer->createBuffer(stagingBuffer,stagingMemory,size,
        vk::BufferUsageFlagBits::eTransferSrc,vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);
        void* data = renderer->lDevice.mapMemory(stagingMemory,0,size);
        memcpy(data,modelMats.data(),size);
        vk::CommandBuffer cb = renderer->startOneShotCommandBuffer(renderer->graphicCommandPool);
        vk::BufferCopy region;
        region.setSize(size);
        region.setSrcOffset(0);
        region.setDstOffset(0);
        cb.copyBuffer(stagingBuffer,modelMatsBuffer,region);
        renderer->finishOneShotCommandBuffer(renderer->graphicCommandPool,cb,renderer->graphicQueue);
        renderer->lDevice.waitIdle();
        renderer->lDevice.unmapMemory(stagingMemory);
        renderer->lDevice.destroyBuffer(stagingBuffer);
        renderer->lDevice.freeMemory(stagingMemory);

        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.setBuffer(modelMatsBuffer);
        bufferInfo.setOffset(0);
        bufferInfo.setRange(size);
        vk::WriteDescriptorSet write;
        write.setBufferInfo(bufferInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eStorageBuffer);
        write.setDstArrayElement(0);
        write.setDstBinding(0);
        write.setDstSet(modelMatsDescriptorSet);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }
    //build vertex buffer
    {
        int size = vertices.size()*sizeof(Vertex);
        renderer->createBuffer(vertexBuffer,vertexBufferMemory,size,
        vk::BufferUsageFlagBits::eVertexBuffer|vk::BufferUsageFlagBits::eTransferDst,vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        renderer->createBuffer(stagingBuffer,stagingMemory,size,
        vk::BufferUsageFlagBits::eTransferSrc,vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);
        void* data = renderer->lDevice.mapMemory(stagingMemory,0,size);
        memcpy(data,vertices.data(),size);
        vk::CommandBuffer cb = renderer->startOneShotCommandBuffer(renderer->graphicCommandPool);
        vk::BufferCopy region;
        region.setSize(size);
        region.setSrcOffset(0);
        region.setDstOffset(0);
        cb.copyBuffer(stagingBuffer,vertexBuffer,region);
        renderer->finishOneShotCommandBuffer(renderer->graphicCommandPool,cb,renderer->graphicQueue);
        renderer->lDevice.waitIdle();
        renderer->lDevice.unmapMemory(stagingMemory);
        renderer->lDevice.destroyBuffer(stagingBuffer);
        renderer->lDevice.freeMemory(stagingMemory);
    }
    //build index buffer
    {
        int size = indexs.size()*sizeof(uint32_t);
        renderer->createBuffer(indexBuffer,indexBufferMemory,size,
        vk::BufferUsageFlagBits::eIndexBuffer|vk::BufferUsageFlagBits::eTransferDst,vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        renderer->createBuffer(stagingBuffer,stagingMemory,size,
        vk::BufferUsageFlagBits::eTransferSrc,vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);
        void* data = renderer->lDevice.mapMemory(stagingMemory,0,size);
        memcpy(data,indexs.data(),size);
        vk::CommandBuffer cb = renderer->startOneShotCommandBuffer(renderer->graphicCommandPool);
        vk::BufferCopy region;
        region.setSize(size);
        region.setSrcOffset(0);
        region.setDstOffset(0);
        cb.copyBuffer(stagingBuffer,indexBuffer,region);
        renderer->finishOneShotCommandBuffer(renderer->graphicCommandPool,cb,renderer->graphicQueue);
        renderer->lDevice.waitIdle();
        renderer->lDevice.unmapMemory(stagingMemory);
        renderer->lDevice.destroyBuffer(stagingBuffer);
        renderer->lDevice.freeMemory(stagingMemory);
    }
    vertices;
    indexs;
}

Node* Scene::loadNode(tinygltf::Node &glTFnode,Node* parent)
{
    Node* newNode = new Node();
    newNode->parent = parent;
    if(glTFnode.matrix.size()){
        newNode->local_transform = glm::make_mat4x4(glTFnode.matrix.data());
    }
    else{
        if(glTFnode.translation.size()){
            newNode->translation = glm::make_vec3(glTFnode.translation.data());
        }
        if(glTFnode.rotation.size()){
            newNode->rotation = glm::make_quat(glTFnode.rotation.data());
        }
        if(glTFnode.scale.size()){
            newNode->scale = glm::make_vec3(glTFnode.scale.data());
        }
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::scale(transform,newNode->scale);
        transform = glm::mat4_cast(newNode->rotation)*transform;
        transform = glm::translate(transform,newNode->translation);
        newNode->local_transform = transform;
    }
    if(parent){
        newNode->global_transform = newNode->local_transform*parent->global_transform;
    }
    else{
        newNode->global_transform = newNode->local_transform;
    }
    if(glTFnode.mesh>-1){
        newNode->mesh = loadMesh(glTFmodel.meshes[glTFnode.mesh],newNode);
    }
    for(int node:glTFnode.children){
        Node* childNode = loadNode(glTFmodel.nodes[node],newNode);
        newNode->children.push_back(childNode);
    }
    return newNode;
}

Mesh* Scene::loadMesh(tinygltf::Mesh &glTFmesh,Node* parent)
{   
    Mesh* newMesh = new Mesh();
    newMesh->parent = parent;
    if(glTFmesh.primitives.size()){
        //a new modelMat is need
        int modelMatID = modelMats.size();
        modelMats.push_back({parent->global_transform});
        for(auto& glTFprimitive:glTFmesh.primitives){
            Primitive* primitive = loadPrimitive(glTFprimitive,modelMatID);
            newMesh->primitives.push_back(primitive);
        }
    }
    return newMesh;
}

Primitive* Scene::loadPrimitive(tinygltf::Primitive &glTFprimitive,int modelMatID)
{
    //load all vertices
    Primitive* newPrimitive = new Primitive();
    int vertexStart = vertices.size();
    int newVertexCount;
    if(glTFprimitive.attributes.find("POSITION")!=glTFprimitive.attributes.end()){
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.attributes["POSITION"]];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        newVertexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newVertexCount;++i){
            Vertex newVertex;
            newVertex.modelMatID = modelMatID;
            newVertex.materialID = glTFprimitive.material;
            newVertex.position = glm::make_vec3(reinterpret_cast<float*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
            vertices.push_back(newVertex);
        }
    }
    else{
        throw std::runtime_error("primitive attribute:POSITION is always needed!");
    }

    if(glTFprimitive.attributes.find("NORMAL")!=glTFprimitive.attributes.end()){
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.attributes["NORMAL"]];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        newVertexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newVertexCount;++i){
            vertices[i+vertexStart].normal = glm::make_vec3(reinterpret_cast<float*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
        }
    }

    if(glTFprimitive.attributes.find("TANGENT")!=glTFprimitive.attributes.end()){
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.attributes["TANGENT"]];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        newVertexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newVertexCount;++i){
            vertices[i+vertexStart].tangent = glm::make_vec3(reinterpret_cast<float*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
        }
    }

    if(glTFprimitive.attributes.find("TEXCOORD_0")!=glTFprimitive.attributes.end()){
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.attributes["TEXCOORD_0"]];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        newVertexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newVertexCount;++i){
            vertices[i+vertexStart].uv0 = glm::make_vec2(reinterpret_cast<float*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
        }
    }

    if(glTFprimitive.attributes.find("TEXCOORD_1")!=glTFprimitive.attributes.end()){
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.attributes["TEXCOORD_1"]];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        newVertexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newVertexCount;++i){
            vertices[i+vertexStart].uv1 = glm::make_vec2(reinterpret_cast<float*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
        }
    }

    newPrimitive->vertexStart = vertexStart;
    newPrimitive->verexCount = newVertexCount;
    //load indices if exist
    if(glTFprimitive.indices>-1){
        newPrimitive->useIndex = true;
        tinygltf::Accessor& glTFaccessor = glTFmodel.accessors[glTFprimitive.indices];
        tinygltf::BufferView&  glTFbufferView = glTFmodel.bufferViews[glTFaccessor.bufferView];
        tinygltf::Buffer& glTFbuffer = glTFmodel.buffers[glTFbufferView.buffer];
        int indexStart = indexs.size();
        int newIndexCount = glTFaccessor.count;
        int byteStride = glTFaccessor.ByteStride(glTFbufferView);
        int byteOffset = glTFaccessor.byteOffset + glTFbufferView.byteOffset;
        for(int i=0;i<newIndexCount;++i){
            uint32_t index;
            if(glTFaccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT){
                index = *(reinterpret_cast<unsigned short*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
            }
            else if(glTFaccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT){
                index = *(reinterpret_cast<unsigned int*>(glTFbuffer.data.data()+byteOffset+byteStride*i));
            }
            else {
                throw std::runtime_error("bad index type!");
            }
            indexs.push_back(vertexStart+index);
        }
        newPrimitive->indexCount = newIndexCount;
        newPrimitive->indexStart = indexStart;
    }
    newPrimitive->material = materials[glTFprimitive.material];
    return newPrimitive;
}

Material* Scene::loadMaterial(tinygltf::Material &glTFmaterial,int index)
{
    Material* newMaterial = new Material();
    newMaterial->index = index;
    newMaterial->properties.basColorFactor = glm::make_vec4(glTFmaterial.pbrMetallicRoughness.baseColorFactor.data());
    newMaterial->properties.emissiveFactor = glm::make_vec3(glTFmaterial.emissiveFactor.data());
    newMaterial->properties.metallicFactor = glTFmaterial.pbrMetallicRoughness.metallicFactor;
    newMaterial->properties.normalScale = glTFmaterial.normalTexture.scale;
    newMaterial->properties.occlusionStength = glTFmaterial.occlusionTexture.strength;
    newMaterial->properties.roughnessFactor = glTFmaterial.pbrMetallicRoughness.roughnessFactor;
    
    if(glTFmaterial.pbrMetallicRoughness.baseColorTexture.index>-1){
        newMaterial->properties.texCoord_baseColor = glTFmaterial.pbrMetallicRoughness.baseColorTexture.texCoord;
        newMaterial->baseColorTexture = textures[glTFmaterial.pbrMetallicRoughness.baseColorTexture.index];
        vk::WriteDescriptorSet write;
        write.setImageInfo(newMaterial->baseColorTexture->desriptorImageInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDstBinding(1);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }
    if(glTFmaterial.emissiveTexture.index>-1){
        newMaterial->properties.texCoord_emissive = glTFmaterial.emissiveTexture.texCoord;
        newMaterial->emissiveTexture = textures[glTFmaterial.emissiveTexture.index];
        vk::WriteDescriptorSet write;
        write.setImageInfo(newMaterial->emissiveTexture->desriptorImageInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDstBinding(2);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }
    if(glTFmaterial.pbrMetallicRoughness.metallicRoughnessTexture.index>-1){
        newMaterial->properties.texCoord_metallicRoughness = glTFmaterial.pbrMetallicRoughness.metallicRoughnessTexture.texCoord;
        newMaterial->metallicRoughnessTexture = textures[glTFmaterial.pbrMetallicRoughness.metallicRoughnessTexture.index];
        vk::WriteDescriptorSet write;
        write.setImageInfo(newMaterial->metallicRoughnessTexture->desriptorImageInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDstBinding(3);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }
    if(glTFmaterial.normalTexture.index>-1){
        newMaterial->properties.texCoord_normal = glTFmaterial.normalTexture.texCoord;
        newMaterial->normalTexture = textures[glTFmaterial.normalTexture.index];
        vk::WriteDescriptorSet write;
        write.setImageInfo(newMaterial->normalTexture->desriptorImageInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDstBinding(4);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }
    if(glTFmaterial.occlusionTexture.index>-1){
        newMaterial->properties.texCoord_occlusion = glTFmaterial.occlusionTexture.texCoord;
        newMaterial->occlusionTexture = textures[glTFmaterial.occlusionTexture.index];
        vk::WriteDescriptorSet write;
        write.setImageInfo(newMaterial->occlusionTexture->desriptorImageInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDstBinding(5);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }

    int size = sizeof(MaterialProperties);
    renderer->createBuffer(newMaterial->uniformMaterialBuffer,newMaterial->uniformMaterialBufferMemory,size,
    vk::BufferUsageFlagBits::eUniformBuffer|vk::BufferUsageFlagBits::eTransferDst,vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingMemory;
    renderer->createBuffer(stagingBuffer,stagingMemory,size,
    vk::BufferUsageFlagBits::eTransferSrc,vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);
    void* data = renderer->lDevice.mapMemory(stagingMemory,0,size);
    memcpy(data,&newMaterial->properties,size);
    vk::CommandBuffer cb = renderer->startOneShotCommandBuffer(renderer->graphicCommandPool);
    vk::BufferCopy region;
    region.setSize(size);
    region.setSrcOffset(0);
    region.setDstOffset(0);
    cb.copyBuffer(stagingBuffer,newMaterial->uniformMaterialBuffer,region);
    renderer->finishOneShotCommandBuffer(renderer->graphicCommandPool,cb,renderer->graphicQueue);
    renderer->lDevice.waitIdle();
    renderer->lDevice.unmapMemory(stagingMemory);
    renderer->lDevice.destroyBuffer(stagingBuffer);
    renderer->lDevice.freeMemory(stagingMemory);
    newMaterial->descriptorBufferInfo.setBuffer(newMaterial->uniformMaterialBuffer);
    newMaterial->descriptorBufferInfo.setOffset(0);
    newMaterial->descriptorBufferInfo.setRange(size);
    {
        vk::WriteDescriptorSet write;
        write.setBufferInfo(newMaterial->descriptorBufferInfo);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eUniformBuffer);
        write.setDstBinding(0);
        write.setDstSet(materialDescriptorSet);
        write.setDstArrayElement(index);
        renderer->lDevice.updateDescriptorSets(1,&write,0,nullptr);
    }

    return newMaterial;
}

Texture* Scene::loadTexture(tinygltf::Texture &glTFtexture,int index)
{
    Texture* newTexture = new Texture();
    newTexture->index = index;

    tinygltf::Image& glTFimage = glTFmodel.images[glTFtexture.source];
    int width = glTFimage.width;
    int height = glTFimage.height;
    newTexture->width = width;
    newTexture->height = height;
    int pixelCount = glTFimage.height*glTFimage.width;

    unsigned char* buffer;
    std::vector<unsigned char> rgba;
    if(glTFimage.component==3){
        rgba.resize(pixelCount*4);
        unsigned char* rgb = glTFimage.image.data();
        for(int i=0;i<pixelCount;++i){
            for(int j=0;j<3;++j){
                rgba[4*i+j] = rgb[3*i+j];
            }
            rgba[4*i+3] = 1;
        }
        buffer = rgba.data();
    }
    else if(glTFimage.component==4){
        buffer = glTFimage.image.data();
    }
    else{
        throw std::runtime_error("bad image componet!");
    }
    renderer->createImage(newTexture->textureImage,newTexture->imageMemory,{(uint32_t)width,(uint32_t)height},vk::Format::eR8G8B8A8Srgb,
    vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled,vk::MemoryPropertyFlagBits::eDeviceLocal);

    vk::Buffer stagingBuffer;
    vk::DeviceMemory stagingMemory;

    renderer->createBuffer(stagingBuffer,stagingMemory,pixelCount*4,vk::BufferUsageFlagBits::eTransferSrc,vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent);

    void* data = renderer->lDevice.mapMemory(stagingMemory,0,pixelCount*4);
    memcpy(data,buffer,pixelCount*4);

    vk::CommandBuffer cb = renderer->startOneShotCommandBuffer(renderer->graphicCommandPool);
    vk::ImageMemoryBarrier imageBarrier;
    imageBarrier.setImage(newTexture->textureImage);
    imageBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.subresourceRange.levelCount = 1;

    imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead);
    imageBarrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
    imageBarrier.setOldLayout(vk::ImageLayout::eUndefined);
    imageBarrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eTransfer,vk::DependencyFlags(0),{},{},imageBarrier);

    vk::BufferImageCopy region;
    region.setImageExtent({(uint32_t)width,(uint32_t)height,1});
    region.setBufferOffset(0);
    region.setImageOffset({0,0});
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageSubresource.mipLevel = 0;
    cb.copyBufferToImage(stagingBuffer,newTexture->textureImage,vk::ImageLayout::eTransferDstOptimal,region);

    imageBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
    imageBarrier.setDstAccessMask(vk::AccessFlags(0));
    imageBarrier.setOldLayout(vk::ImageLayout::eUndefined);
    imageBarrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eTopOfPipe,vk::DependencyFlags(0),{},{},imageBarrier);

    renderer->finishOneShotCommandBuffer(renderer->graphicCommandPool,cb,renderer->graphicQueue);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.setMagFilter(vk::Filter::eLinear);
    samplerInfo.setMinFilter(vk::Filter::eLinear); 
    newTexture->imageSampler = renderer->lDevice.createSampler(samplerInfo);

    newTexture->textureImageView = renderer->createImageView(newTexture->textureImage,vk::Format::eR8G8B8A8Srgb,vk::ImageAspectFlagBits::eColor);

    newTexture->desriptorImageInfo.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    newTexture->desriptorImageInfo.setImageView(newTexture->textureImageView);
    newTexture->desriptorImageInfo.setSampler(newTexture->imageSampler);

    renderer->lDevice.unmapMemory(stagingMemory);
    renderer->lDevice.destroyBuffer(stagingBuffer);
    renderer->lDevice.freeMemory(stagingMemory);
    return newTexture;
}
Mesh::~Mesh()
{
    for(int i=0;i<primitives.size();++i){
        delete primitives[i];
    }
}
Node::~Node()
{
    delete mesh;
    for(int i=0;i<children.size();++i){
        delete children[i];
    }
}
}
