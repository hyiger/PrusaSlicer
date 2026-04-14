#version 140

uniform float overlay_alpha;

// x = tainted, y = specular;
in vec2 intensity;
in vec3 vertex_color;

out vec4 out_color;

void main()
{
    out_color = vec4(vec3(intensity.y) + vertex_color * intensity.x, overlay_alpha);
}
