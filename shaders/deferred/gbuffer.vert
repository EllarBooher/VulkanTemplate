#version 460

#extension GL_EXT_buffer_reference2 : require

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec2 outTexCoord;
layout(location = 2) out vec3 outNormal;

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

layout(push_constant) uniform PushConstant
{
    VertexBuffer vertexBuffer;
    ModelBuffer modelBuffer;
    ModelInverseTransposeBuffer modelInverseTransposeBuffer;
    vec2 padding0;
    mat4 cameraProjView;
} pc;

void main()
{
    mat4 model = pc.modelBuffer.models[gl_InstanceIndex];
    mat4 modelInverseTranspose = pc.modelInverseTransposeBuffer.modelInverseTransposes[gl_InstanceIndex];

    const Vertex vertex = pc.vertexBuffer.vertices[gl_VertexIndex];

    vec4 worldPosition = model * vec4(vertex.position, 1.0);
    outWorldPosition = worldPosition.xyz;

    gl_Position = pc.cameraProjView * worldPosition;

    outNormal = normalize((modelInverseTranspose * vec4(vertex.normal, 0.0)).xyz);

    outTexCoord = vec2(vertex.uv_x, vertex.uv_y);
}