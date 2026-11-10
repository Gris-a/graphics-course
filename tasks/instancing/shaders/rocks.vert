#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
struct Instance { mat4 transform; vec4 color; };
layout(set = 0, binding = 0, std430) readonly buffer Instances { Instance data[]; } instances;
layout(push_constant) uniform Params { mat4 projView; vec4 cameraPosition; vec4 light; } params;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) out vec3 tint;
void main()
{
    Instance instance = instances.data[gl_InstanceIndex];
    worldPosition = (instance.transform * vec4(position, 1.0)).xyz;
    worldNormal = normalize(mat3(instance.transform) * normal);
    uv = texcoord;
    tint = instance.color.rgb;
    gl_Position = params.projView * vec4(worldPosition, 1.0);
}
