
#version 450

#extension GL_ARB_separate_shader_objects : enable


layout(location = 0) out vec4 out_fragColor;


layout(location = 0) in vec2 uv;
layout(location = 1) in float height;
layout(location = 2) in vec3 worldPosition;


layout(push_constant) uniform params_t
{
    mat4 projView;
    vec4 terrainParams;
    vec4 lightingParams;
    vec4 cameraPosition;
} params;


layout(set = 0, binding = 0) uniform sampler2D heightmap;
layout(set = 0, binding = 1) uniform sampler2D sandAlbedo;
layout(set = 0, binding = 2) uniform sampler2D grassAlbedo;
layout(set = 0, binding = 3) uniform sampler2D rockAlbedo;
layout(set = 0, binding = 4) uniform sampler2D snowAlbedo;
layout(set = 0, binding = 5) uniform sampler2D sandRoughness;
layout(set = 0, binding = 6) uniform sampler2D grassRoughness;
layout(set = 0, binding = 7) uniform sampler2D rockRoughness;
layout(set = 0, binding = 8) uniform sampler2D snowRoughness;
layout(set = 0, binding = 9) uniform sampler2D sandHeight;
layout(set = 0, binding = 10) uniform sampler2D grassHeight;
layout(set = 0, binding = 11) uniform sampler2D rockHeight;
layout(set = 0, binding = 12) uniform sampler2D snowHeight;

const float PI = 3.14159265359;

float distributionGGX(float nDotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}


void main()
{
    float worldSize = params.terrainParams.x;
    float heightScale = params.terrainParams.y;
    float textureScale = params.terrainParams.z;
    float bumpStrength = params.terrainParams.w;

    vec2 mapSize = vec2(textureSize(heightmap, 0));
    vec2 mapTexel = 1.0 / mapSize;
    vec2 worldStep = vec2(worldSize) / (mapSize - 1.0);

    float left = texture(heightmap, uv - vec2(mapTexel.x, 0.0)).r;
    float right = texture(heightmap, uv + vec2(mapTexel.x, 0.0)).r;
    float down = texture(heightmap, uv - vec2(0.0, mapTexel.y)).r;
    float up = texture(heightmap, uv + vec2(0.0, mapTexel.y)).r;

    float dHdx = (right - left) * heightScale / (2.0 * worldStep.x);
    float dHdz = (up - down) * heightScale / (2.0 * worldStep.y);
    vec3 normal = normalize(vec3(-dHdx, 1.0, -dHdz));

    vec2 materialUv = worldPosition.xz * textureScale;
    vec4 weights = vec4(
        1.0 - smoothstep(0.20, 0.30, height),
        smoothstep(0.15, 0.25, height) *
            (1.0 - smoothstep(0.50, 0.60, height)),
        smoothstep(0.40, 0.60, height) *
            (1.0 - smoothstep(0.75, 0.85, height)),
        smoothstep(0.70, 0.85, height)
    );
    weights /= max(dot(weights, vec4(1.0)), 0.0001);

    vec3 albedo =
        texture(sandAlbedo, materialUv).rgb * weights.x +
        texture(grassAlbedo, materialUv).rgb * weights.y +
        texture(rockAlbedo, materialUv).rgb * weights.z +
        texture(snowAlbedo, materialUv).rgb * weights.w;

    float roughness = (
        texture(sandRoughness, materialUv).r * weights.x +
        texture(grassRoughness, materialUv).r * weights.y +
        texture(rockRoughness, materialUv).r * weights.z +
        texture(snowRoughness, materialUv).r * weights.w
    );
    roughness = clamp(roughness, 0.0, 1.0);

    float materialHeight =
        texture(sandHeight, materialUv).r * weights.x +
        texture(grassHeight, materialUv).r * weights.y +
        texture(rockHeight, materialUv).r * weights.z +
        texture(snowHeight, materialUv).r * weights.w;
    float dHeightDx = dFdx(materialHeight);
    float dHeightDz = dFdy(materialHeight);
    vec2 dUvDx = dFdx(materialUv);
    vec2 dUvDz = dFdy(materialUv);
    float uvDeterminant =
        dUvDx.x * dUvDz.y - dUvDz.x * dUvDx.y;
    float detailX = 0.0;
    float detailZ = 0.0;
    if (abs(uvDeterminant) > 0.000001)
    {
        detailX =
            (dHeightDx * dUvDz.y - dHeightDz * dUvDx.y) /
            uvDeterminant * textureScale * bumpStrength;
        detailZ =
            (dUvDx.x * dHeightDz - dUvDz.x * dHeightDx) /
            uvDeterminant * textureScale * bumpStrength;
    }

    vec3 tangent = normalize(vec3(1.0, dHdx, 0.0));
    vec3 bitangent = normalize(cross(tangent, normal));
    normal = normalize(
        tangent * -detailX +
        normal -
        bitangent * detailZ
    );

    vec3 viewDir = normalize(params.cameraPosition.xyz - worldPosition);
    float azimuth = params.lightingParams.x;
    float elevation = params.lightingParams.y;
    vec3 lightDir = normalize(vec3(
        cos(azimuth) * cos(elevation),
        sin(elevation),
        sin(azimuth) * cos(elevation)
    ));
    vec3 halfDir = normalize(viewDir + lightDir);

    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    float nDotH = max(dot(normal, halfDir), 0.0);
    float hDotV = max(dot(halfDir, viewDir), 0.0);
    vec3 f0 = vec3(0.04);
    vec3 fresnel = fresnelSchlick(hDotV, f0);
    float distribution = distributionGGX(nDotH, roughness);
    float geometry =
        geometrySchlickGGX(nDotV, roughness) *
        geometrySchlickGGX(nDotL, roughness);
    vec3 specular = distribution * geometry * fresnel /
        max(4.0 * nDotV * nDotL, 0.001);
    vec3 diffuseWeight = 1.0 - fresnel;
    vec3 color = albedo * 0.03 +
        (diffuseWeight * albedo / PI + specular) *
        params.lightingParams.z * nDotL;

    out_fragColor = vec4(
        color,
        1.0
    );
}
