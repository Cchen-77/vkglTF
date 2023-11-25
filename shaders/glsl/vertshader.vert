#version 450 
layout(location=0) out vec3 outColor;

vec3 hardCodedVertex[3] = {
    {-0.5,-0.5,0.0},
    {0.5,-0.5,0.0},
    {0,0.5,0}
};
void main(){
    gl_Position = vec4(hardCodedVertex[gl_VertexIndex],1);
    outColor = vec3(1.0,0.0,0.0);
}