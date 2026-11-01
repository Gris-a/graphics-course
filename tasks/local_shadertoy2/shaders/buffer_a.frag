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

    float m = 0.5 + 0.5 * sin(20.0 * uv.y + 2.0 * pc.iTime);

    fragColor = vec4(
        mix(
            vec3(1.0, 0.2, 0.2),
            vec3(0.2, 0.5, 1.0),
            m
        ),
        1.0
    );
}