#version 140

uniform float overlay_alpha;
uniform float u_contour_interval; // mm between contour lines; 0 = disabled
uniform float u_contour_darkness; // 0..1 blend factor for line color

// x = tainted, y = specular;
in vec2 intensity;
in vec3 vertex_color;
in float raw_z_mm;

out vec4 out_color;

void main()
{
    vec3 shaded = vec3(intensity.y) + vertex_color * intensity.x;

    // Iso-Z contour lines. Compute how close this fragment's raw_z is to a
    // multiple of u_contour_interval, expressed in units of the screen-space
    // derivative of raw_z. That screen-space width is what makes the lines
    // a consistent pixel width regardless of Z-exaggeration or zoom.
    if (u_contour_interval > 0.0) {
        float interval = u_contour_interval;
        float f        = raw_z_mm / interval;
        float dist     = abs(fract(f + 0.5) - 0.5);   // 0 on line, 0.5 between
        float pixel_w  = fwidth(f);                   // screen-space derivative
        // Anti-aliased 1-pixel-wide line:
        float line_t   = 1.0 - smoothstep(0.0, pixel_w, dist);
        shaded = mix(shaded, shaded * (1.0 - u_contour_darkness), line_t);
    }

    out_color = vec4(shaded, overlay_alpha);
}
