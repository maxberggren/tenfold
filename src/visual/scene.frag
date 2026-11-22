#version 440
// The world: five raymarched signed-distance worlds the camera flies
// through, morphing into each other as `world` slides between them, each
// shaped by the music and by the hand.
//
//   0 tunnel     a twisting tube lined with hashed panels (the classic
//                demo tunnel, carved rather than textured)
//   1 mandelbox  a cave inside Tom Lowe's mandelbox, repeated along the
//                flight path; its scale is the hand's fold
//   2 crystal    a field of kaleidoscopic IFS crystals (Knighty's KIFS:
//                abs-folds and rotations, the rotations from the chord)
//   3 city       a canyon of blocks, one lane per part, each as tall as
//                its part is loud: the arrangement as a skyline
//   4 lattice    a twisted gyroid lattice, opened round the flight path
//
// Two worlds blend by mixing their distances (a morph, not a crossfade),
// and the kaleidoscope folds space round the flight axis before any of
// them, so every world also comes in 3- to 12-fold symmetry.
//
// Lighting is cheap and loud, as in a 4k intro: a tetrahedral normal, a
// light at the camera and one that follows the lead's pitch, fresnel rims,
// step-count occlusion and a glow summed along the ray from how close it
// passed to anything. Emitters are summed on the way too: the snare's ring,
// the voice's filament, hats glinting off edges.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 res;    // width, height (pixels), time (s), beat
    vec4 drums;  // kick, snare, hat, perc (envelopes)
    vec4 drums2; // sub (808), snares counted, kicks counted, master level
    vec4 bass;   // level, pitch, filter, envelope
    vec4 chord;  // hue, change flash, level, tones
    vec4 lead;   // level, pitch, brightness, bend
    vec4 arp;    // envelope, pitch, note flash, notes a second
    vec4 atmos;  // level, swell, halo, tone
    vec4 fx;     // build, drop flash, sweep, wash
    vec4 vocal;  // level, pitch, clarity, loops
    vec4 levels0; // drums, bass, chords, lead levels
    vec4 levels1; // arp, atmos, fx, vocal levels
    vec4 morph0; // world, fold, symmetry, seed
    vec4 morph1; // yaw, pitch, roll, zoom
    vec4 morph2; // distance flown, warp, hue turn, chaos
    vec4 morph3; // march steps, glow, hand pressure, fingers down
} ubuf;

#define PI 3.14159265
#define TAU 6.2831853

mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, s, -s, c);
}

float hash1(float n) { return fract(sin(n) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }

// Inigo Quilez's cosine palette, turned by the chord's hue.
vec3 palette(float t) {
    float h = ubuf.chord.x + ubuf.morph2.z;
    return 0.5 + 0.5 * cos(TAU * (vec3(0.0, 0.33, 0.67) + t + h));
}

// The parts' own phosphors (the page colours), so each instrument's mark
// is in the colour its page is drawn in.
#define C_DRUMS vec3(1.0, 0.54, 0.12)
#define C_BASS vec3(1.0, 0.31, 0.85)
#define C_CHORDS vec3(0.18, 0.9, 1.0)
#define C_LEAD vec3(1.0, 0.69, 0.0)
#define C_ARP vec3(0.22, 1.0, 0.53)
#define C_ATMOS vec3(0.66, 0.55, 1.0)
#define C_FX vec3(1.0, 0.25, 0.21)
#define C_VOCAL vec3(1.0, 0.48, 0.72)

float level(float lane) {
    if (lane < 0.5) return ubuf.levels0.x;
    if (lane < 1.5) return ubuf.levels0.y;
    if (lane < 2.5) return ubuf.levels0.z;
    if (lane < 3.5) return ubuf.levels0.w;
    if (lane < 4.5) return ubuf.levels1.x;
    if (lane < 5.5) return ubuf.levels1.y;
    if (lane < 6.5) return ubuf.levels1.z;
    return ubuf.levels1.w;
}

vec3 laneColour(float lane) {
    if (lane < 0.5) return C_DRUMS;
    if (lane < 1.5) return C_BASS;
    if (lane < 2.5) return C_CHORDS;
    if (lane < 3.5) return C_LEAD;
    if (lane < 4.5) return C_ARP;
    if (lane < 5.5) return C_ATMOS;
    if (lane < 6.5) return C_FX;
    return C_VOCAL;
}

// --- the worlds ---------------------------------------------------------------
// Each returns a distance, and in trap a number 0..1 that colours the hit.

float tunnel(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    p.xy = rot(p.z * (0.06 + 0.05 * sin(seed)) + ubuf.drums2.x * 0.4 * sin(p.z * 0.7)) * p.xy;
    float r = length(p.xy);
    float a = atan(p.y, p.x);
    float R = 2.4 + 0.35 * ubuf.bass.x * sin(p.z * 0.9 + ubuf.res.z * 2.0);
    // Panels on the wall: cells round and along the tube, each pushed in by
    // its own hash, more and deeper as the hand folds.
    float n = 10.0 + floor(ubuf.morph0.y * 14.0);
    vec2 cell = vec2(a / TAU * n, p.z * 0.9);
    vec2 id = floor(cell);
    vec2 f = fract(cell) - 0.5;
    float h = hash2(id + floor(seed * 3.0));
    float inset = 0.12 + 0.55 * ubuf.morph0.y * h;
    float panel = smoothstep(0.42, 0.36, max(abs(f.x), abs(f.y)));
    // The kick lights the ring of panels it reaches as a wave down the tube.
    trap = h;
    float d = R - r - inset * panel;
    return d * 0.6;
}

float mandelbox(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    // A negative scale: caverns; scaled so the camera's path runs through
    // its halls, repeated down the path.
    float k = 0.25;
    p *= k;
    p.z = mod(p.z, 8.0) - 4.0;
    float scale = -1.6 - 0.8 * ubuf.morph0.y + 0.15 * sin(seed * 0.7) - 0.08 * ubuf.bass.x;
    vec3 z = p;
    float dr = 1.0;
    float t = 1e9;
    for (int i = 0; i < 7; i++) {
        z = clamp(z, -1.0, 1.0) * 2.0 - z;
        float r2 = dot(z, z);
        t = min(t, r2);
        if (r2 < 0.3) {
            z *= 3.333;
            dr *= 3.333;
        } else if (r2 < 1.0) {
            z /= r2;
            dr /= r2;
        }
        z = z * scale + p;
        dr = dr * abs(scale) + 1.0;
    }
    trap = clamp(t * 0.6, 0.0, 1.0);
    return length(z) / abs(dr) / k;
}

float crystal(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    // Crystals at the cells' centres, the path along their corners, so it
    // threads between them.
    p.z = mod(p.z, 6.0) - 3.0;
    p.xy = mod(p.xy, 6.0) - 3.0;
    p *= 0.5;
    // The folds' angles follow the chord: a new chord turns every crystal.
    // Kept within half a radian: turned further, the folds fold the
    // crystal away to dust.
    float a1 = 0.45 * sin(ubuf.chord.x * TAU + seed * 0.31);
    float a2 = 0.3 * sin(seed * 0.17 + ubuf.morph0.y * 2.0);
    mat2 r1 = rot(a1);
    mat2 r2 = rot(a2);
    float sc = 1.9 + 0.5 * ubuf.morph0.y;
    vec3 off = vec3(1.0, 0.8 + 0.3 * sin(seed), 0.6);
    float s = 1.0;
    float t = 0.0;
    for (int i = 0; i < 6; i++) {
        p = abs(p);
        if (p.x < p.y) p.xy = p.yx;
        if (p.x < p.z) p.xz = p.zx;
        if (p.y < p.z) p.yz = p.zy;
        p.xy = r1 * p.xy;
        p.yz = r2 * p.yz;
        p = p * sc - off * (sc - 1.0);
        s *= sc;
        t += length(p) * 0.02;
    }
    trap = fract(t);
    vec3 q = abs(p) - vec3(1.0);
    return (length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0)) / s / 0.5;
}

float city(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    vec2 id = floor(p.xz);
    vec2 f = fract(p.xz) - 0.5;
    // Lanes out from the road: one per part, both sides.
    float lane = clamp(floor(abs(p.x)) - 1.0, 0.0, 7.0);
    float road = step(abs(p.x), 1.0);
    float lv = level(lane);
    float h = 0.15 + 1.4 * ubuf.morph0.y * hash2(id + floor(seed * 2.0)) + 2.2 * lv;
    // The kick ripples down the street.
    h += 0.4 * ubuf.drums.x * sin(id.y * 0.8 - ubuf.res.z * 6.0);
    h *= 1.0 - road;
    vec2 b = abs(f) - vec2(0.38);
    float box2 = length(max(b, 0.0)) + min(max(b.x, b.y), 0.0);
    float floorY = -1.6;
    float ceilY = 1.6 + 0.8 * ubuf.morph0.y;
    float lower = max(box2, p.y - (floorY + h));
    float upper = max(box2, (ceilY - h * 0.6) - p.y);
    float d = min(lower, upper);
    d = min(d, p.y - floorY + 0.02);
    d = min(d, ceilY + 0.6 - p.y);
    // Never step past the cell: the next one may be taller.
    vec2 edge = 0.5 - abs(f);
    d = min(d, max(min(edge.x, edge.y), 0.0) + 0.05);
    trap = (lane + 0.5) / 8.0;
    return d;
}

float lattice(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    p.xy = rot(p.z * (0.12 + 0.1 * ubuf.morph0.y) + seed) * p.xy;
    float sc = 1.1 + 1.6 * ubuf.morph0.y + 0.2 * sin(seed * 1.3);
    vec3 q = p * sc;
    float g = abs(dot(sin(q), cos(q.yzx))) - 0.18 - 0.25 * ubuf.bass.x;
    trap = fract(dot(floor(q / PI), vec3(0.13, 0.27, 0.41)));
    return g / sc * 0.6;
}

float world(int w, vec3 p, out float trap) {
    if (w == 0) return tunnel(p, trap);
    if (w == 1) return mandelbox(p, trap);
    if (w == 2) return crystal(p, trap);
    if (w == 3) return city(p, trap);
    return lattice(p, trap);
}

// The kaleidoscope: space folded round the flight axis into n wedges.
vec3 kaleido(vec3 p) {
    float s = ubuf.morph0.z;
    if (s < 0.05) return p;
    float n = s < 0.3 ? 3.0 : s < 0.5 ? 4.0 : s < 0.7 ? 6.0 : s < 0.9 ? 8.0 : 12.0;
    float a = atan(p.y, p.x);
    float seg = TAU / n;
    a = mod(a + seg * 0.5, seg) - seg * 0.5;
    a = abs(a);
    float r = length(p.xy);
    return vec3(r * cos(a), r * sin(a), p.z);
}

float map(vec3 p, out float trap) {
    vec3 q = kaleido(p);
    // The bass bends space: a slow sine field as deep as it is loud, as fine
    // as it is high.
    float f = 1.0 + 3.0 * ubuf.bass.y;
    q += 0.12 * (ubuf.bass.x + ubuf.drums2.x) * sin(q.zxy * f + ubuf.res.z * 1.7);
    float w = ubuf.morph0.x;
    int w0 = int(floor(w));
    int w1 = int(mod(floor(w) + 1.0, 5.0));
    float u = smoothstep(0.0, 1.0, fract(w));
    float t0 = 0.0, t1 = 0.0;
    float d = world(w0, q, t0);
    if (u > 0.002) {
        float d1 = world(w1, q, t1);
        d = mix(d, d1, u);
        t0 = mix(t0, t1, u);
    }
    trap = t0;
    // The flight path stays open whatever the world.
    float flight = 0.55 - length(p.xy);
    return max(d, flight);
}

vec3 normalAt(vec3 p) {
    vec2 e = vec2(0.0015, -0.0015);
    float t;
    return normalize(e.xyy * map(p + e.xyy, t) + e.yyx * map(p + e.yyx, t) + e.yxy * map(p + e.yxy, t) +
                     e.xxx * map(p + e.xxx, t));
}

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = ubuf.res.x / max(ubuf.res.y, 1.0);
    vec2 sp = (uv - 0.5) * vec2(aspect, -1.0);
    float time = ubuf.res.z;

    // The camera: on the axis, flown forward; the hand turns it, the kick
    // punches the lens, a build shakes it.
    float kick = ubuf.drums.x;
    float build = ubuf.fx.x;
    vec3 ro = vec3(0.0, 0.0, ubuf.morph2.x);
    vec2 shake = build * build * 0.04 * vec2(hash1(time * 61.0) - 0.5, hash1(time * 47.0) - 0.5);
    float fov = 1.1 + 0.45 * ubuf.morph1.w - 0.22 * kick + 0.35 * build;
    vec3 rd = normalize(vec3(sp + shake, fov));
    rd.xy = rot(ubuf.morph1.z) * rd.xy;
    rd.yz = rot(ubuf.morph1.y * 0.6) * rd.yz;
    rd.xz = rot(ubuf.morph1.x * 0.8) * rd.xz;

    // The lead's light: ahead of the camera, as high as the lead sings.
    vec3 leadPos = ro + vec3(1.2 * sin(time * 0.7), (ubuf.lead.y - 0.5) * 2.6, 2.5);

    float t = 0.05;
    float glow = 0.0;
    vec3 emit = vec3(0.0);
    float trap = 0.0;
    bool hit = false;
    int steps = int(ubuf.morph3.x);
    float iters = 0.0;
    for (int i = 0; i < 160; i++) {
        if (i >= steps) break;
        vec3 p = ro + rd * t;
        float d = map(p, trap);
        // Neon: how close the ray grazed an edge, summed (only real near
        // misses count, so it lines edges instead of hazing everything).
        glow += exp(-max(d, 0.0) * 70.0) * 0.006;
        // The voice: a filament down the flight path, wavering with its pitch.
        vec2 wob = 0.18 * vec2(sin(p.z * (2.0 + 6.0 * ubuf.vocal.y) + time * 3.0), cos(p.z * 1.7 + time * 2.1));
        float fil = length(p.xy - wob) - 0.01 - 0.06 * ubuf.vocal.x;
        emit += C_VOCAL * ubuf.vocal.x * exp(-max(fil, 0.0) * 30.0) * 0.05;
        // The lead's light as a small sun.
        float ls = length(p - leadPos);
        emit += C_LEAD * ubuf.lead.x * exp(-ls * 6.0) * 0.06;
        iters += 1.0;
        if (d < 0.0012 * t) {
            hit = true;
            break;
        }
        t += d;
        if (t > 40.0) break;
    }

    vec3 col = vec3(0.0);
    vec3 windows = vec3(0.0);
    // Fog: near black, tinted by the chord, lifted by the atmosphere.
    vec3 fogCol = palette(0.15) * 0.025 + C_ATMOS * (0.12 * ubuf.atmos.x + 0.25 * ubuf.atmos.y);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = normalAt(p);
        vec3 base = palette(trap * 1.6 + 0.1 * sin(p.z * 0.2));
        base *= base; // deeper, more saturated than the palette's pastels
        // The city's lanes are their parts' colours.
        float city = 1.0 - clamp(abs(ubuf.morph0.x - 3.0), 0.0, 1.0);
        float lane = clamp(floor(abs(p.x)) - 1.0, 0.0, 7.0);
        base = mix(base, laneColour(lane) * (0.35 + level(lane)), city * step(1.0, abs(p.x)));
        // Its towers' windows light up with their part, the street's grid
        // with the beat.
        vec2 win = fract(vec2(p.z * 6.0 + p.x * 6.0, p.y * 5.0));
        float lit = step(0.35, win.x) * step(0.4, win.y) * step(0.5, hash3(floor(p * vec3(6.0, 5.0, 6.0))));
        float side = 1.0 - abs(n.y);
        windows = laneColour(lane) * lit * side * (0.25 + 2.5 * level(lane)) * city * step(1.0, abs(p.x));
        vec2 gridL = abs(fract(p.xz) - 0.5);
        windows += palette(0.2) * smoothstep(0.47, 0.5, max(gridL.x, gridL.y)) * abs(n.y) * city *
                   (0.3 + 2.0 * ubuf.drums.x);
        vec3 l1 = normalize(ro + vec3(0.0, 0.6, 0.0) - p);
        vec3 l2 = normalize(leadPos - p);
        float dif = max(dot(n, l1), 0.0);
        float dif2 = max(dot(n, l2), 0.0) / (1.0 + 0.2 * dot(leadPos - p, leadPos - p));
        float fres = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
        float ao = 1.0 - iters / float(max(steps, 1));
        vec3 refl = reflect(rd, n);
        vec3 l3 = normalize(vec3(0.4, 0.8, 0.3));
        float dif3 = max(dot(n, l3), 0.0);
        float spec = pow(max(dot(refl, l1), 0.0), 24.0) + 0.5 * pow(max(dot(refl, l3), 0.0), 16.0);
        col = base * (0.05 + 1.1 * dif + 0.5 * dif3) * ao;
        col += vec3(1.0) * spec * 0.6 * ao;
        col += C_LEAD * dif2 * ubuf.lead.x * (1.0 + 2.0 * ubuf.lead.z);
        col += windows;
        col += palette(0.5 + trap) * fres * (0.25 + 1.2 * ubuf.chord.y) * ao;
        // The snare: a ring of light rushing away down the path.
        float ringZ = ro.z + 1.5 + (1.0 - ubuf.drums.y) * 14.0;
        col += vec3(1.4, 1.3, 1.2) * ubuf.drums.y * exp(-abs(p.z - ringZ) * 3.0);
        // The kick: the panels flare.
        col *= 1.0 + 1.6 * kick * smoothstep(0.4, 1.0, trap);
        // Hats glint off edges: tiny cells that wink on each hit.
        float g = hash3(floor(p * 18.0) + floor(time * 30.0));
        col += vec3(1.0) * ubuf.drums.z * step(0.993 - 0.02 * fres, g) * 3.0;
        // Arp notes light cells in the arp's green.
        float ga = hash3(floor(p * 6.0) + floor(ubuf.arp.y * 24.0));
        col += C_ARP * ubuf.arp.z * step(0.93, ga) * 2.0;
        // The fog swallows what is far.
        float fog = 1.0 - exp(-t * (0.035 + 0.08 * ubuf.atmos.x));
        col = mix(col, fogCol, fog);
    } else {
        col = fogCol;
    }
    col += glow * palette(0.3 + glow * 2.0) * (0.8 + 3.0 * kick + 1.5 * ubuf.morph3.y + 2.0 * ubuf.chord.y);
    col += emit;
    // A build whitens everything toward the drop.
    col = mix(col, vec3(dot(col, vec3(0.33))) * 1.3 + build * 0.2, build * 0.6);
    fragColor = vec4(col, 1.0) * ubuf.qt_Opacity;
}
