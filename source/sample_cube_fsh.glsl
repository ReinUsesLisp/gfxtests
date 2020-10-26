#version 460

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube texture0;

void main()
{
    outColor = texture(texture0, vec3(1, 0, 0));
}
