#version 460

#define WITH_NORMAL_MAP_UNSIGNED
#define WITH_NORMAL_MAP_GREEN_UP

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 outDiffuseColor;
layout(location = 1) out vec4 outSpecularColor;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outWorldPosition;
layout(location = 4) out vec4 outOcclusionRoughnessMetallic;

void main()
{
    outWorldPosition = vec4(inWorldPosition, 1.0);
    outNormal = vec4(inNormal, 0.0);

    const vec4 sampledColor = vec4(inNormal * 0.5 + 0.5, 1.0);

    outDiffuseColor = vec4(sampledColor.rgb, 1.0);
    outSpecularColor = vec4(sampledColor.rgb, 1.0);

    outOcclusionRoughnessMetallic = vec4(1.0, 0.25, 0.0, 0.0);
}