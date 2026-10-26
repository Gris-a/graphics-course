#version 450

layout(push_constant) uniform PushConstants
{
    vec2 iResolution;
    float iTime;
} pc;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / pc.iResolution;

    float r = 0.3 + 0.65 * (
        0.5 + 0.5 * sin(12.0 * uv.x - 1.5 * pc.iTime)
    );

    float m = 0.5 + 0.5 * sin(8.0 * uv.y + pc.iTime);

    fragColor = vec4(r, m, 0.0, 1.0);
}