#version 440
// One direction of the bloom's blur, at a quarter of the size: nine taps of
// a Gaussian, the first pass keeping only what is bright.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 dir; // the step between taps (in texture units), then threshold, gain
} ubuf;
layout(binding = 1) uniform sampler2D src;

vec3 bright(vec3 c) {
    float l = max(c.r, max(c.g, c.b));
    return c * smoothstep(ubuf.dir.z, ubuf.dir.z + 0.5, l);
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 d = ubuf.dir.xy;
    vec3 c = bright(texture(src, uv).rgb) * 0.2270;
    c += bright(texture(src, uv + d * 1.3846).rgb) * 0.3162;
    c += bright(texture(src, uv - d * 1.3846).rgb) * 0.3162;
    c += bright(texture(src, uv + d * 3.2308).rgb) * 0.0703;
    c += bright(texture(src, uv - d * 3.2308).rgb) * 0.0703;
    fragColor = vec4(c * ubuf.dir.w, 1.0) * ubuf.qt_Opacity;
}
