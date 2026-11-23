#version 440
// The feedback buffer: last frame, zoomed, turned, warped and faded, under
// this frame's world, with the 2D marks drawn in so they leave trails too.
// The oldest trick in the demo book (a frame fed back into itself), here
// steered by the music and by every finger: each finger down is a
// whirlpool in the flow, as strong as it presses.
//
// Drawn on top, each in its part's colour:
//   kick    pushes the echoes in (its shockwave is post.frag's)
//   vocal   a ring at the centre, its edge the microphone's waveform
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 res;    // width, height, time, beat
    vec4 drums;  // kick, snare, hat, perc
    vec4 drums2; // sub, snares counted, kicks counted, master
    vec4 bass;   // level, pitch, filter, envelope
    vec4 chord;  // hue, change flash, level, tones
    vec4 lead;   // level, pitch, brightness, bend
    vec4 arp;    // envelope, pitch, flash, rate
    vec4 atmos;  // level, swell, halo, tone
    vec4 fx;     // build, drop, sweep, wash
    vec4 vocal;  // level, pitch, clarity, loops
    vec4 morph0; // world, fold, symmetry, seed
    vec4 morph1; // yaw, pitch, roll, zoom
    vec4 morph2; // distance flown, warp, hue turn, chaos
    vec4 morph3; // steps, glow, pressure, fingers
    vec4 roll;   // roll speed (radians a frame), trails, markers, crt
    vec4 f0;     // fingers: x, y (0..1, y up), pressure, alive
    vec4 f1;
    vec4 f2;
    vec4 f3;
    vec4 f4;
    vec4 f5;
    vec4 f6;
    vec4 f7;
    vec4 f8;
    vec4 f9;
    vec4 n0;     // notes, newest first: age (s), pitch, velocity, kind (0 arp, 1 lead)
    vec4 n1;
    vec4 n2;
    vec4 n3;
    vec4 debug;  // x: 3 leaves the echoes out, 4 draws them alone (TENFOLD_VISUAL_STAGE); y: the fingers drawn
    vec4 fh0;    // each finger's turn of the colour wheel from the chord's: f0..f3
    vec4 fh1;    // f4..f7
    vec4 fh2;    // f8, f9
} ubuf;
layout(binding = 1) uniform sampler2D scene;
layout(binding = 2) uniform sampler2D prev;
layout(binding = 3) uniform sampler2D wave;

#define PI 3.14159265
#define TAU 6.2831853
#define C_DRUMS vec3(1.0, 0.54, 0.12)
#define C_LEAD vec3(1.0, 0.69, 0.0)
#define C_ARP vec3(0.22, 1.0, 0.53)
#define C_ATMOS vec3(0.66, 0.55, 1.0)
#define C_VOCAL vec3(1.0, 0.48, 0.72)
#define C_BASS vec3(1.0, 0.31, 0.85)

mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

float hash1(float n) { return fract(sin(n) * 43758.5453); }

vec3 palette(float t) {
    float h = ubuf.chord.x + ubuf.morph2.z;
    return 0.5 + 0.5 * cos(TAU * (vec3(0.0, 0.33, 0.67) + t + h));
}

// One finger's whirlpool on the point q (screen units, centred).
vec2 swirl(vec2 q, vec4 f, float aspect, float sense) {
    if (f.w <= 0.0) return q;
    vec2 c = (f.xy - 0.5) * vec2(aspect, 1.0);
    vec2 d = q - c;
    float r2 = dot(d, d);
    float s = f.w * (0.25 + f.z) * 0.09 * exp(-r2 * 10.0);
    return c + rot(s * sense) * d * (1.0 - s * 0.25);
}

float fingerGlow(vec2 p, vec4 f, float aspect) {
    if (f.w <= 0.0) return 0.0;
    vec2 c = (f.xy - 0.5) * vec2(aspect, 1.0);
    vec2 d = p - c;
    // A small bright ring, not a blob: the finger's place in the flow.
    float r = length(d);
    return f.w * (0.3 + f.z) * exp(-abs(r - 0.018) * 260.0);
}

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = ubuf.res.x / max(ubuf.res.y, 1.0);
    float time = ubuf.res.z;
    // Centred, y up, height 1.
    vec2 p = (uv - 0.5) * vec2(aspect, -1.0);

    // Where last frame's pixel comes from: in toward the centre (the
    // trails stream outward, a tunnel of echoes), turned with the roll,
    // through every finger's whirlpool, warped by a moving field.
    vec2 q = p;
    float kick = ubuf.drums.x;
    q *= 1.0 - (0.006 + 0.02 * ubuf.morph1.w * ubuf.morph1.w + 0.035 * kick + 0.03 * ubuf.fx.x);
    q = rot(ubuf.roll.x + 0.01 * ubuf.drums2.x) * q;
    q = swirl(q, ubuf.f0, aspect, 1.0);
    q = swirl(q, ubuf.f1, aspect, -1.0);
    q = swirl(q, ubuf.f2, aspect, 1.0);
    q = swirl(q, ubuf.f3, aspect, -1.0);
    q = swirl(q, ubuf.f4, aspect, 1.0);
    q = swirl(q, ubuf.f5, aspect, -1.0);
    q = swirl(q, ubuf.f6, aspect, 1.0);
    q = swirl(q, ubuf.f7, aspect, -1.0);
    q = swirl(q, ubuf.f8, aspect, 1.0);
    q = swirl(q, ubuf.f9, aspect, -1.0);
    float warp = ubuf.morph2.y;
    q += (0.002 + 0.012 * warp) * vec2(sin(q.y * 9.0 + time * 1.3 + ubuf.morph0.w), cos(q.x * 7.0 - time * 1.1));
    vec2 puv = q / vec2(aspect, -1.0) + 0.5;
    vec3 old = texture(prev, puv).rgb;
    // The trails: short unless the hand asks, longer in a wash, and wiped
    // clean on a drop, so the next world starts clear.
    float keep = mix(0.55, 0.9, ubuf.roll.y) + 0.04 * ubuf.fx.w;
    keep *= 1.0 - step(0.75, ubuf.fx.y);
    old = max(old * clamp(keep, 0.0, 0.95) - 0.004, 0.0);

    vec4 sceneNow = texture(scene, uv);
    vec3 now = sceneNow.rgb;
    // Where the world lights itself (alpha under 1), no echoes: they would
    // ghost the lead's ribbon into stripes.
    float echoes = sceneNow.a;
    float lum = dot(now, vec3(0.3, 0.5, 0.2));
    // The marks of this frame, added to the world before it meets the
    // echoes: added after, a mark drawn every frame would pile up to many
    // times its brightness.
    vec3 col = vec3(0.0);

    // (The kick's shockwave is the glass's, in post.frag: drawn here, its
    // sweep would fill a disc in the echoes.)
    float r = length(p);
    // Vocal: a ring whose edge is the microphone's waveform.
    float va = atan(p.y, p.x);
    float wave = texture(wave, vec2(fract(va / TAU + 0.5), 0.25)).r * 2.0 - 1.0;
    float vr = 0.14 + 0.2 * ubuf.vocal.x + wave * 0.1 * ubuf.vocal.x;
    col += C_VOCAL * ubuf.vocal.x * exp(-abs(r - vr) * 140.0) * 1.4;
    col += C_VOCAL * ubuf.vocal.x * ubuf.vocal.z * exp(-r * r * 40.0) * 0.3;


    // The fingers, drawn into the flow so their paths trail.
    // Each in the chord's colour, turned as far round the wheel as the
    // player set that finger (visual.h, Settings::fingerHues).
    vec3 fg = palette(0.6 + ubuf.fh0.x) * fingerGlow(p, ubuf.f0, aspect) +
              palette(0.6 + ubuf.fh0.y) * fingerGlow(p, ubuf.f1, aspect) +
              palette(0.6 + ubuf.fh0.z) * fingerGlow(p, ubuf.f2, aspect) +
              palette(0.6 + ubuf.fh0.w) * fingerGlow(p, ubuf.f3, aspect) +
              palette(0.6 + ubuf.fh1.x) * fingerGlow(p, ubuf.f4, aspect) +
              palette(0.6 + ubuf.fh1.y) * fingerGlow(p, ubuf.f5, aspect) +
              palette(0.6 + ubuf.fh1.z) * fingerGlow(p, ubuf.f6, aspect) +
              palette(0.6 + ubuf.fh1.w) * fingerGlow(p, ubuf.f7, aspect) +
              palette(0.6 + ubuf.fh2.x) * fingerGlow(p, ubuf.f8, aspect) +
              palette(0.6 + ubuf.fh2.y) * fingerGlow(p, ubuf.f9, aspect);
    col += fg * ubuf.roll.z * ubuf.debug.y;

    now += col;
    // The echoes only where this frame is dark: they fill the black between
    // things and never muddy the things themselves.
    col = max(now, old * (1.0 - smoothstep(0.03, 0.35, lum)) * echoes);
    if (ubuf.debug.x == 3.0) col = now;
    if (ubuf.debug.x == 4.0) col = old;
    fragColor = vec4(min(col, vec3(8.0)), 1.0) * ubuf.qt_Opacity;
}
