#version 460

layout(location = 0) in vec2 texcoord;
layout(location = 1) in float lay;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2DArray texture0;

void main()
{
	vec2 coord = (texcoord + vec2(1.0)) * 0.5;
	coord = coord * 1.25 - vec2(0.125);

    outColor = textureLod(texture0, vec3(coord, lay), 0);
}
