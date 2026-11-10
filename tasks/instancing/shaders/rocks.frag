#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 tint;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D albedoMap;
layout(set = 0, binding = 2) uniform sampler2D normalMap;
layout(set = 0, binding = 3) uniform sampler2D armMap;
layout(push_constant) uniform Params { mat4 projView; vec4 cameraPosition; vec4 light; } params;
const float PI = 3.14159265359;
void main()
{
    vec3 n = normalize(worldNormal);
    vec3 dpdx = dFdx(worldPosition);
    vec3 dpdy = dFdy(worldPosition);
    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    if (abs(determinant) > 1e-8)
    {
        vec3 t = (dpdx * duvdy.y - dpdy * duvdx.y) / determinant;
        t = normalize(t - n * dot(n, t));
        vec3 sourceB = (dpdy * duvdx.x - dpdx * duvdy.x) / determinant;
        vec3 b = normalize(cross(n, t));
        b *= dot(b, sourceB) < 0.0 ? -1.0 : 1.0;
        n = normalize(mat3(t, b, n) * (texture(normalMap, uv).xyz * 2.0 - 1.0));
    }
    vec3 albedo = texture(albedoMap, uv).rgb * tint;
    vec3 arm = texture(armMap, uv).rgb;
    float roughness = clamp(arm.g, 0.045, 1.0);
    vec3 v = normalize(params.cameraPosition.xyz - worldPosition);
    vec3 l = vec3(cos(params.light.x) * cos(params.light.y), sin(params.light.y), sin(params.light.x) * cos(params.light.y));
    vec3 h = normalize(v + l);
    float nv = max(dot(n, v), 0.0);
    float nl = max(dot(n, l), 0.0);
    float nh = max(dot(n, h), 0.0);
    float hv = max(dot(h, v), 0.0);
    float a2 = pow(roughness, 4.0);
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(PI * denominator * denominator, 1e-7);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float geometry = nv / max(nv * (1.0 - k) + k, 1e-5) * nl / max(nl * (1.0 - k) + k, 1e-5);
    vec3 fresnel = vec3(0.04) + vec3(0.96) * pow(1.0 - hv, 5.0);
    vec3 specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.001);
    vec3 color = albedo * 0.03 * arm.r + ((1.0 - fresnel) * albedo / PI + specular) * params.light.z * nl;
    outColor = vec4(color, 1.0);
}
