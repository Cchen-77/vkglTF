#version 450 
layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec3 inTangent;
layout(location=3) in vec2 inUV0;
layout(location=4) in vec2 inUV1;
layout(location=5) in int inMaterialID;
layout(location=6) in int inModelMatID;

layout(location=1) out vec3 outNormal;
layout(location=2) out vec3 outTangent;
layout(location=3) out vec2 outUV0;
layout(location=4) out vec2 outUV1;
layout(location=5) flat out int outMaterialID;

layout(set=0,binding=0) buffer SSBO{
    mat4 modelMats[];
};
layout(push_constant) uniform PC{
    vec3 cameraPostion;
    vec3 viewDirection;
    mat4 viewMat;
    mat4 projectionMat;
};
void main(){
    gl_Position = projectionMat*viewMat*modelMats[inModelMatID]*vec4(inPosition,1);
    outUV0 = inUV0;
    outUV1 = inUV1;
    outMaterialID = inMaterialID;
    outTangent = inTangent;
    outNormal = inNormal;
    
}