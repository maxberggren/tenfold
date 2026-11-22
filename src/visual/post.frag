#version 440
// The glass: what the feedback made, with its bloom, through a CRT like
// the window's own: barrel curve, chromatic fringes, scanlines every three
// pixels, a vignette and grain. The music breaks the glass: snares and
// kicks split the colours, quick fingers and drops tear rows sideways, a
// drop flashes white and, at its peak, inverts.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 res;    // width, height, time, beat
    vec4 drums;  // kick, snare, hat, perc
    vec4 chord;  // hue, change flash, level, tones
    vec4 fx;     // build, drop, sweep, wash
    vec4 atmos;  // level, swell, halo, tone
    vec4 lead;   // level, pitch, brightness, bend
    vec4 drums2; // sub, snares counted, kicks counted, master
    vec4 morph2; // distance flown, warp, hue turn, chaos
    vec4 roll;   // roll speed, trails, markers, crt (0 flat .. 1 a full CRT)
} ubuf;
layout(binding = 1) uniform sampler2D image;
layout(binding = 2) uniform sampler2D bloom;

#define TAU 6.2831853
vec3 palette(float t) {
    float h = ubuf.chord.x + ubuf.morph2.z;
    return 0.5 + 0.5 * cos(TAU * (vec3(0.0, 0.33, 0.67) + t + h));
}

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = qt_TexCoord0;
    float crt = ubuf.roll.w;
    float time = ubuf.res.z;
    vec2 d = uv - 0.5;
    // The tube's curve.
    uv = 0.5 + d * (1.0 + crt * 0.09 * dot(d, d));
    float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);

    // Torn rows: quick hands and drops.
    float chaos = clamp(ubuf.morph2.w + ubuf.fx.y * 0.6, 0.0, 1.0);
    float row = floor(uv.y * 24.0);
    float tear = step(1.0 - chaos * 0.6, hash2(vec2(row, floor(time * 14.0))));
    uv.x += tear * (hash2(vec2(row * 1.7, floor(time * 9.0))) - 0.5) * 0.12 * chaos;

    // The kick: a shockwave that bends the glass as it rushes out, a thin
    // ring of the drums' orange on its front.
    float kick = ubuf.drums.x;
    float aspect = ubuf.res.x / max(ubuf.res.y, 1.0);
    vec2 pc = (uv - 0.5) * vec2(aspect, 1.0);
    float r = length(pc);
    float front = (1.0 - kick) * 0.9;
    float wave = exp(-abs(r - front) * 28.0) * kick * kick;
    uv -= pc / max(r, 1e-3) * wave * 0.035 / vec2(aspect, 1.0);

    // The snare slides the shutter's rows sideways.
    uv.x += step(0.72, fract(uv.y * 9.0 + ubuf.drums2.y * 0.37)) * ubuf.drums.y * ubuf.drums.y * 0.02;

    // Chromatic fringes, wider toward the edge, split by the drums.
    vec2 off = d * (0.003 + 0.012 * ubuf.drums.y + 0.008 * ubuf.drums.x + 0.02 * chaos);
    vec3 col;
    col.r = texture(image, uv + off).r;
    col.g = texture(image, uv).g;
    col.b = texture(image, uv - off).b;
    vec3 b = texture(bloom, uv).rgb;
    col += b * (0.45 + 1.2 * ubuf.drums.x + 0.8 * ubuf.fx.x);
    col += vec3(1.0, 0.54, 0.12) * exp(-abs(r - front) * 160.0) * kick * kick * 1.5;

    // The atmosphere: thin filaments of plasma drifting through the dark
    // (here on the glass, not in the echoes, which would smear them).
    float time2 = time * 0.35;
    float pl = sin(pc.x * 3.1 + time2) + sin(pc.y * 4.3 - time2 * 1.3) + sin((pc.x + pc.y) * 2.7 + time2 * 0.7) +
               sin(length(pc + vec2(sin(time2 * 0.6), cos(time2 * 0.4))) * 5.0 - time2 * 2.0);
    float band = pow(0.5 + 0.5 * sin(pl * 2.3), 14.0);
    vec3 plasma = mix(vec3(0.66, 0.55, 1.0), palette(pl * 0.12), 0.5) * band;
    float lum = dot(col, vec3(0.3, 0.5, 0.2));
    col += plasma * (0.02 + 0.1 * ubuf.atmos.x + 0.6 * ubuf.atmos.y + 0.3 * ubuf.atmos.z) * (1.0 - clamp(lum * 3.0, 0.0, 1.0));

    // The lead: a thin laser across the glass at its pitch, wavering with
    // its bend, wider and whiter as the finger brightens it.
    float ly = (ubuf.lead.y - 0.5) * 0.75 + 0.025 * sin(pc.x * 7.0 + time * 4.0) * (1.0 + 4.0 * abs(ubuf.lead.w));
    float lw = 260.0 / (1.0 + 2.0 * ubuf.lead.z);
    float laser = exp(-abs(pc.y - ly) * lw);
    col += mix(vec3(1.0, 0.69, 0.0), vec3(1.0, 0.95, 0.8), laser * laser) * laser * ubuf.lead.x * 1.3;
    // The snare: a shutter, for a blink: bands of the picture flash and
    // slide, shifted every hit.
    float snare = ubuf.drums.y * ubuf.drums.y;
    float bars = step(0.72, fract(uv.y * 9.0 + ubuf.drums2.y * 0.37));
    col += vec3(1.0, 0.97, 0.92) * snare * bars * 0.22;

    // Chords: each change washes the glass in the new chord's colour.
    col += palette(0.0) * ubuf.chord.y * 0.12;

    // A drop: white, then for a moment the negative.
    float drop = ubuf.fx.y;
    col = mix(col, vec3(1.6), drop * 0.55);

    // Filmic: a black level (the echoes and the glow lift it), a soft
    // shoulder, a touch more colour.
    col = max(col - 0.025, 0.0) * 1.03;
    col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
    col = mix(vec3(dot(col, vec3(0.3, 0.55, 0.15))), col, 1.25);
    col = mix(col, 1.0 - col, smoothstep(0.82, 0.95, drop) * 0.85);

    // Scanlines every three pixels (the window's own), a vignette, grain that
    // the hats stir.
    float y = qt_TexCoord0.y * ubuf.res.y;
    col *= 1.0 - crt * 0.2 * step(fract(y / 3.0), 0.34);
    col *= 1.0 - crt * 0.55 * dot(d, d) * 1.6;
    col += (hash2(qt_TexCoord0 * ubuf.res.xy + fract(time * 7.13) * 91.0) - 0.5) * (0.03 + 0.06 * ubuf.drums.z);
    col *= inside;
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0) * ubuf.qt_Opacity;
}
