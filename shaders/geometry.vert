#version 460

#extension GL_EXT_buffer_reference2 : require

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

layout(buffer_reference, std430) readonly buffer ModelInverseTransposeBuffer
{
    mat4 modelInverseTransposes[];
};

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform PushConstant
{
    VertexBuffer vertexBuffer;
    ModelBuffer modelBuffer;
    ModelInverseTransposeBuffer modelInverseTransposeBuffer;
    mat4 cameraProjView;
} pc;

void main()
{
    const mat4 model = pc.modelBuffer.models[gl_InstanceIndex];
    const mat4 modelInverseTranspose = pc.modelInverseTransposeBuffer.modelInverseTransposes[gl_InstanceIndex];

    const Vertex vertex = pc.vertexBuffer.vertices[gl_VertexIndex];
    
    vec4 position = pc.cameraProjView * model * vec4(vertex.position, 1.0);
    const vec4 worldNormal = modelInverseTranspose * vec4(vertex.normal, 1.0);

    gl_Position = position;
    outColor = vec3(worldNormal.xyz);
    
}