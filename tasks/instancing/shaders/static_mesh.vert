
#version 450

#extension GL_ARB_separate_shader_objects : enable


layout(location = 0) in vec2 gridUv;


layout(push_constant) uniform params_t
{
    mat4 projView;
    vec4 terrainParams;
    vec4 lightingParams;
    vec4 cameraPosition;
} params;


layout(set = 0, binding = 0) uniform sampler2D heightmap;


layout(location = 0) out vec2 uv;
layout(location = 1) out float height;
layout(location = 2) out vec3 worldPosition;


out gl_PerVertex
{
    vec4 gl_Position;
};


void main()
{
    const float worldSize = params.terrainParams.x;
    const float heightScale = params.terrainParams.y;

    const vec2 mapSize = vec2(
        textureSize(heightmap, 0)
    );

    uv = (
        gridUv * (mapSize - 1.0) + 0.5
    ) / mapSize;

    height = textureLod(
        heightmap,
        uv,
        0.0
    ).r;

    worldPosition = vec3(
        (gridUv.x - 0.5) * worldSize,
        height * heightScale,
        (gridUv.y - 0.5) * worldSize
    );

    gl_Position = params.projView * vec4(
        worldPosition,
        1.0
    );
}
