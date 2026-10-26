#version 450

#define PI 3.14159265

layout(push_constant) uniform PushConstants
{
    vec2 iResolution;
    float iTime;
} pc;

layout(set = 0, binding = 0) uniform sampler2D iChannel0;
layout(set = 0, binding = 1) uniform sampler2D iChannel1;

layout(location = 0) out vec4 fragColor;

vec3 eye = vec3(0.0, 0.0, 3.0);
vec3 light = vec3(5.0, 5.0, 5.0);
vec3 lightColor = vec3(1.0);

float sdSphere(vec3 p, vec3 c, float r)
{
    return length(p - c) - r + 0.1 * sin(pc.iTime);
}

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float sdf(in vec3 p)
{
    float s1 = sdSphere(p, vec3(-0.8, 0.0, 0.0), 0.7);
    float s2 = sdSphere(p, vec3( 0.8, 0.0, 0.0), 0.7);

    float a = pc.iTime * 0.8;
    float s3 = sdSphere(
        p,
        vec3(cos(a) * 1.6, sin(a) * 1.6, 0.0),
        0.55
    );

    return smin(smin(s1, s2, 0.4), s3, 0.4);
}

vec3 calcNormal(in vec3 p)
{
    vec2 e = vec2(0.001, 0.0);

    return normalize(vec3(
        sdf(p + e.xyy) - sdf(p - e.xyy),
        sdf(p + e.yxy) - sdf(p - e.yxy),
        sdf(p + e.yyx) - sdf(p - e.yyx)
    ));
}

vec2 sphereUV(vec3 p)
{
    vec3 d = normalize(p);

    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;

    return vec2(u, v);
}

vec3 albedo(in vec3 p)
{
    return texture(iChannel0, sphereUV(p)).rgb;
}

vec2 roughMetal(in vec3 p)
{
    vec2 rm = texture(iChannel1, sphereUV(p)).rg;
    return clamp(rm, 0.0, 1.0);
}

vec3 pbr(
    in vec3 p,
    in vec3 n,
    in vec3 alb,
    in float rough,
    in float metal
)
{
    vec3 l = normalize(light - p);
    vec3 v = normalize(eye - p);
    vec3 h = normalize(l + v);

    float NdotL = max(dot(n, l), 0.0);
    float NdotV = max(dot(n, v), 1e-4);
    float NdotH = max(dot(n, h), 0.0);
    float VdotH = max(dot(v, h), 0.0);

    float a = rough * rough;
    float a2 = a * a;

    float NDF = a2 / (
        PI * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0)
    );

    float k = a * 0.5;

    float G =
        (NdotL / (NdotL * (1.0 - k) + k)) *
        (NdotV / (NdotV * (1.0 - k) + k));

    vec3 F0 = mix(vec3(0.04), alb, metal);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    vec3 spec = NDF * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metal);

    return (kd * alb / PI + spec) * lightColor * NdotL + 0.03 * alb;
}

vec3 trace(in vec3 from, in vec3 dir, out bool hit)
{
    vec3 p = from;
    hit = false;
    float t = 0.0;

    for (int i = 0; i < 10000; ++i)
    {
        float d = sdf(p);

        if (d < 0.01)
        {
            hit = true;
            break;
        }

        t += d;

        if (t > 1000.0)
            break;

        p += d * dir;
    }

    return p;
}

void main()
{
    vec2 scale =
        9.0 * pc.iResolution.xy /
        max(pc.iResolution.x, pc.iResolution.y);

    vec2 uv = scale * (
        gl_FragCoord.xy / pc.iResolution - 0.5
    );

    vec3 dir = normalize(vec3(uv, 0.0) - eye);

    bool hit;
    vec3 p = trace(eye, dir, hit);

    vec3 col = vec3(0.05, 0.07, 0.10);

    if (hit)
    {
        vec3 n = calcNormal(p);
        vec3 alb = albedo(p);
        vec2 rm = roughMetal(p);

        col = pbr(p, n, alb, rm.x, rm.y);
    }

    fragColor = vec4(col, 1.0);
}