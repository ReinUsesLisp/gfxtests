#version 460

layout (location = 0) out vec4 outColor[8];

void main()
{
    for (int i = 0; i < 8; ++i) {
        outColor[i] = vec4(gl_FragCoord.xy / vec2(8.0), float(i) / 8.0, 1.0);
    }
}
