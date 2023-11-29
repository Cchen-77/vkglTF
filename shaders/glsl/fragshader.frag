#version 450
#define MAX_MATERIAL_COUNT 128
layout(location=1) in vec3 inNormal;
layout(location=2) in vec3 inTangent;
layout(location=3) in vec2 inUV0;
layout(location=4) in vec2 inUV1;
layout(location=5) flat in int inMaterialID;

layout(set=1,binding=0) uniform MaterialProperties{
    vec4 basColorFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStength;
    vec3 emissiveFactor;
    int texCoord_baseColor;
    int texCoord_metallicRoughness;
    int texCoord_normal;
    int texCoord_occlusion;
    int texCoord_emissive;
} mateiralProps[MAX_MATERIAL_COUNT];
layout(set=1,binding=1) uniform sampler2D baseColorTextures[MAX_MATERIAL_COUNT];
layout(set=1,binding=2) uniform sampler2D metallicRoughnessTextures[MAX_MATERIAL_COUNT];
layout(set=1,binding=3) uniform sampler2D normalTextures[MAX_MATERIAL_COUNT];
layout(set=1,binding=4) uniform sampler2D occlusionTextures[MAX_MATERIAL_COUNT];
layout(set=1,binding=5) uniform sampler2D emissiveTextures[MAX_MATERIAL_COUNT];

layout(location=0) out vec4 outColor;
void main(){
    vec2 uv[2] = {inUV0,inUV1};
    vec4 color;
    if(mateiralProps[inMaterialID].texCoord_baseColor>-1){
        color = texture(baseColorTextures[inMaterialID],uv[mateiralProps[inMaterialID].texCoord_baseColor]);
    }
    else{
        color = mateiralProps[inMaterialID].basColorFactor;
    }
    //color = vec4(inNormal/2+0.5,1);
    outColor = color;
}