#version 460
#extension GL_AMD_vertex_shader_layer : require

layout(location = 0) out vec2 texcoord;
layout(location = 1) out float instance;

out gl_PerVertex {
	vec4 gl_Position;
};

void main()
{
    float x = -1.0 + float((gl_VertexID & 1) << 2);
    float y = -1.0 + float((gl_VertexID & 2) << 1);
    gl_Position = vec4(x, y, 0, 1);
    instance = gl_InstanceID;
    texcoord = vec2(x, y);
}
