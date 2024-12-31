#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_ARB_shading_language_include : require

struct Vertex
{
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer ModelBuffer
{
    mat4 models[];
};

layout(push_constant) uniform PushConstant
{
    VertexBuffer vertexBuffer;
    ModelBuffer modelBuffer;
    
    mat4 lightProjView;
} pc;

void main()
{
    mat4 model = pc.modelBuffer.models[gl_InstanceIndex];

    Vertex vertex = pc.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = pc.lightProjView * model * vec4(vertex.position, 1.0);
}