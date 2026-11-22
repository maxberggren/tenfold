#version 440
// The world: five raymarched signed-distance worlds the camera flies
// through, morphing into each other as `world` slides between them.
//
//   0 city       a canyon of towers, one lane per part each side of the
//                road, as tall as its part is loud, its windows lit by its
//                notes: the arrangement as a skyline
//   1 tunnel     a twisting tube of hashed panels (the classic demo tunnel,
//                carved rather than textured)
//   2 crystal    a field of kaleidoscopic IFS crystals (Knighty's KIFS:
//                abs-folds and rotations, the rotations from the chord)
//   3 sponge     the halls of an endless Menger sponge, leaning and staggered
//   4 lattice    a twisted gyroid lattice, opened round the flight path
//
// Two worlds blend by mixing their distances (a morph, not a crossfade).
// Before any of them, space itself is bent by the hand and the music:
// folded into a kaleidoscope, twisted along the path, scaled, inverted in
// the circle round the path (four fingers turn the tunnel inside out),
// shoved by the kick, fractured into slabs by the snare, swelled by the bass.
//
// The look is dark by design: each world is one hue (the chord's) at a low
// value with its complement on the rims, so the saturated light is the
// instruments' own, in their pages' colours: the lead a ribbon of light
// through the world at its pitch, arp notes orbs that light the walls as
// the camera passes, the voice a filament down the path, hats glints on
// edges, the kick a flare along every edge. With only drums playing the
// world is near black, lit by the kick.
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
    vec4 morph2; // distance flown, warp (inversion), hue turn, chaos
    vec4 morph3; // march steps, glow, flight speed (units a second), fingers down
    vec4 n0;     // notes, newest first: age (s), pitch, velocity, kind (0 arp, 1 lead)
    vec4 n1;
    vec4 n2;
    vec4 n3;
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

// Inigo Quilez's cosine palette, turned by the chord's hue: t near 0 is the
// chord's colour, t = 0.5 its complement.
vec3 palette(float t) {
    float h = ubuf.chord.x + ubuf.morph2.z;
    return 0.5 + 0.5 * cos(TAU * (vec3(0.0, 0.33, 0.67) + t + h));
}

// The parts' own phosphors (the page colours).
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

// --- the worlds -----------------------------------------------------------------
// Each returns a distance, and in trap a number 0..1 that shades the hit.

float city(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    vec2 id = floor(p.xz);
    vec2 f = fract(p.xz) - 0.5;
    // Lanes out from the road: one per part, both sides.
    float lane = clamp(floor(abs(p.x)) - 1.0, 0.0, 7.0);
    float road = step(abs(p.x), 1.0);
    float lv = level(lane);
    float h = 0.15 + 1.4 * ubuf.morph0.y * hash2(id + floor(seed * 2.0)) + 2.2 * lv;
    // The bass lifts the whole skyline; the kick ripples down the street.
    h += 1.1 * ubuf.bass.w + 0.5 * ubuf.drums.x * sin(id.y * 0.8 - ubuf.res.z * 6.0);
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

float tunnel(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    p.xy = rot(p.z * (0.06 + 0.05 * sin(seed))) * p.xy;
    float r = length(p.xy);
    float a = atan(p.y, p.x);
    float R = 2.4;
    // Panels on the wall: cells round and along the tube, each pushed in by
    // its own hash, more and deeper as the hand folds.
    float n = 10.0 + floor(ubuf.morph0.y * 14.0);
    vec2 cell = vec2(a / TAU * n, p.z * 0.9);
    vec2 id = floor(cell);
    vec2 f = fract(cell) - 0.5;
    float h = hash2(id + floor(seed * 3.0));
    float inset = 0.12 + 0.55 * ubuf.morph0.y * h;
    float panel = smoothstep(0.42, 0.36, max(abs(f.x), abs(f.y)));
    trap = h;
    return (R - r - inset * panel) * 0.6;
}

float crystal(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    // Crystals at the cells' centres, the path along their corners, so it
    // threads between them.
    p.z = mod(p.z, 6.0) - 3.0;
    p.xy = mod(p.xy, 6.0) - 3.0;
    p *= 0.42;
    // Kept within half a radian: turned further, the folds fold the crystal
    // away to dust. The chord turns them.
    float a1 = 0.45 * sin(ubuf.chord.x * TAU + seed * 0.31);
    float a2 = 0.3 * sin(seed * 0.17 + ubuf.morph0.y * 2.0);
    mat2 r1 = rot(a1);
    mat2 r2 = rot(a2);
    float sc = 1.9 + 0.5 * ubuf.morph0.y;
    vec3 off = vec3(1.0, 0.8 + 0.3 * sin(seed), 0.6);
    float s = 1.0;
    float t = 0.0;
    // Three folds: big facets, not dust.
    for (int i = 0; i < 3; i++) {
        p = abs(p);
        if (p.x < p.y) p.xy = p.yx;
        if (p.x < p.z) p.xz = p.zx;
        if (p.y < p.z) p.yz = p.zy;
        p.xy = r1 * p.xy;
        p.yz = r2 * p.yz;
        p = p * sc - off * (sc - 1.0);
        s *= sc;
        t += length(p) * 0.05;
    }
    trap = fract(t);
    // An octahedron at the end, for a gem's facets.
    float oct = (abs(p.x) + abs(p.y) + abs(p.z) - 1.6) * 0.57735;
    return oct / s / 0.42;
}

float sponge(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    // An endless Menger sponge, its big crossing halls along the path: cut
    // three times over, each cut turned a little by the seed and pushed by
    // the fold, so the halls lean and stagger into new architecture.
    float k = 0.32;
    p *= k;
    float d = -1.0;
    float s = 1.0;
    float t = 0.0;
    mat2 r = rot(0.12 * sin(seed * 0.9));
    for (int m = 0; m < 4; m++) {
        vec3 a = mod(p * s + vec3(0.0, 0.0, 0.3 * ubuf.morph0.y * float(m)), 2.0) - 1.0;
        s *= 3.0;
        vec3 q = abs(1.0 - 3.0 * abs(a));
        float da = max(q.x, q.y), db = max(q.y, q.z), dc = max(q.z, q.x);
        float c = (min(da, min(db, dc)) - 1.0) / s;
        if (c > d) {
            d = c;
            t = float(m) / 3.0;
        }
        p.xy = r * p.xy;
    }
    trap = t;
    return d / k;
}

float lattice(vec3 p, out float trap) {
    float seed = ubuf.morph0.w;
    p.xy = rot(p.z * (0.12 + 0.1 * ubuf.morph0.y) + seed) * p.xy;
    float sc = 1.1 + 1.6 * ubuf.morph0.y + 0.2 * sin(seed * 1.3);
    vec3 q = p * sc;
    float g = abs(dot(sin(q), cos(q.yzx))) - 0.18;
    trap = fract(dot(floor(q / PI), vec3(0.13, 0.27, 0.41)));
    return g / sc * 0.6;
}

float world(int w, vec3 p, out float trap) {
    if (w == 0) return city(p, trap);
    if (w == 1) return tunnel(p, trap);
    if (w == 2) return crystal(p, trap);
    if (w == 3) return sponge(p, trap);
    return lattice(p, trap);
}

// --- space, bent ------------------------------------------------------------------

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

// Bends space; returns how much it stretched there, so the march steps short.
float bend(inout vec3 q) {
    float stretch = 1.0;
    // The roll twists space along the path, not just the camera.
    q.xy = rot(q.z * 0.05 * clamp(ubuf.morph1.z, -6.0, 6.0)) * q.xy;
    // The kick shoves the walls in; the snare breaks the world into slabs
    // that jump sideways.
    q.xy *= 1.0 + 0.12 * ubuf.drums.x;
    float slab = floor(q.z * 1.3);
    float jump = step(0.55, hash1(slab + ubuf.drums2.y * 7.31));
    q.xy += jump * ubuf.drums.y * 0.5 * (vec2(hash1(slab * 3.1), hash1(slab * 5.7)) - 0.5);
    // Two fingers' pinch scales the world itself.
    float s = exp2(-ubuf.morph1.w);
    q *= s;
    stretch /= s;
    // Four fingers' warp: the circle round the path inverted, the far walls
    // brought to the middle and the middle thrown out to the walls.
    // (A little warp, the default, only stirs the echoes: the inversion
    // starts past a third.)
    float w = smoothstep(0.3, 1.0, ubuf.morph2.y);
    if (w > 0.002) {
        float r2 = max(dot(q.xy, q.xy), 0.05);
        float R2 = 2.6;
        vec2 inv = q.xy * (R2 / r2);
        q.xy = mix(q.xy, inv, w);
        stretch *= mix(1.0, min(r2 / R2, 1.0), w) * 0.85;
    }
    // The bass swells space: a slow sine field as deep as it is loud, as
    // fine as it is high.
    float f = 0.7 + 2.5 * ubuf.bass.y;
    q += 0.2 * (ubuf.bass.w + ubuf.drums2.x) * sin(q.zxy * f + ubuf.res.z * 1.7);
    return stretch;
}

float map(vec3 p, out float trap) {
    vec3 q = kaleido(p);
    float stretch = bend(q);
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
    d *= stretch * (1.0 - 0.3 * ubuf.drums.y);
    // The flight path stays open whatever the world.
    return max(d, 0.55 - length(p.xy));
}

vec3 normalAt(vec3 p) {
    vec2 e = vec2(0.002, -0.002);
    float t;
    return normalize(e.xyy * map(p + e.xyy, t) + e.yyx * map(p + e.yyx, t) + e.yxy * map(p + e.yxy, t) +
                     e.xxx * map(p + e.xxx, t));
}

// Five steps out along the normal: how buried the point is.
float occlusion(vec3 p, vec3 n) {
    float o = 0.0, w = 1.0, t;
    for (int i = 1; i <= 5; i++) {
        float h = 0.06 * float(i);
        o += w * (h - map(p + n * h, t));
        w *= 0.6;
    }
    return clamp(1.0 - 3.0 * o, 0.0, 1.0);
}

// The lead's ribbon: where it crosses the plane at depth z.
vec2 ribbon(float z, float time) {
    return vec2(1.1 * sin(z * 0.45 + time * 0.6),
                (ubuf.lead.y - 0.5) * 2.4 + (0.15 + 0.5 * abs(ubuf.lead.w)) * sin(z * 1.4 + time * 2.0));
}

// An arp note's orb: placed ahead when it played, then left behind as the
// camera flies on (age seconds at its speed).
vec3 orb(vec4 n, float camZ) {
    float x = (n.y - 0.5) * 3.2;
    float y = 0.9 * sin(n.y * 23.0 + n.z * 7.0);
    return vec3(x, y, camZ + 4.0 - n.x * ubuf.morph3.z);
}

float orbOn(vec4 n) { return n.w < 0.5 ? n.z * max(0.0, 1.0 - n.x / 1.6) : 0.0; }

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
    float fov = 1.15 - 0.28 * kick + 0.4 * build;
    vec3 rd = normalize(vec3(sp + shake, fov));
    rd.xy = rot(ubuf.morph1.z) * rd.xy;
    rd.yz = rot(ubuf.morph1.y * 0.6) * rd.yz;
    rd.xz = rot(ubuf.morph1.x * 0.8) * rd.xz;

    vec3 leadPos = vec3(ribbon(ro.z + 3.0, time), ro.z + 3.0);
    vec3 o0 = orb(ubuf.n0, ro.z), o1 = orb(ubuf.n1, ro.z), o2 = orb(ubuf.n2, ro.z), o3 = orb(ubuf.n3, ro.z);
    float e0 = orbOn(ubuf.n0), e1 = orbOn(ubuf.n1), e2 = orbOn(ubuf.n2), e3 = orbOn(ubuf.n3);

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
        // Neon: how close the ray grazed an edge.
        glow += exp(-max(d, 0.0) * 70.0) * 0.006;
        // The lead's ribbon of light through the world.
        float lr = length(p.xy - ribbon(p.z, time)) - 0.012;
        emit += C_LEAD * ubuf.lead.x * (1.0 + 2.0 * ubuf.lead.z) * exp(-max(lr, 0.0) * 40.0) * 0.06 *
                smoothstep(0.5, 2.0, p.z - ro.z);
        // The voice: a filament down the flight path, wavering with its pitch.
        vec2 wob = 0.18 * vec2(sin(p.z * (2.0 + 6.0 * ubuf.vocal.y) + time * 3.0), cos(p.z * 1.7 + time * 2.1));
        float fil = length(p.xy - wob) - 0.01 - 0.05 * ubuf.vocal.x;
        emit += C_VOCAL * ubuf.vocal.x * exp(-max(fil, 0.0) * 30.0) * 0.05;
        // The arp's orbs.
        emit += C_ARP * (e0 * exp(-length(p - o0) * 9.0) + e1 * exp(-length(p - o1) * 9.0) +
                         e2 * exp(-length(p - o2) * 9.0) + e3 * exp(-length(p - o3) * 9.0)) * 0.12;
        iters += 1.0;
        if (d < 0.0018 * t) {
            hit = true;
            break;
        }
        t += d;
        if (t > 40.0) break;
    }

    // How much is playing beyond the drums: the world's own light. Drums
    // alone, it is near black and the kick lights it.
    float harmony = clamp(ubuf.levels0.y + ubuf.levels0.z + ubuf.levels1.y + 0.6 * ubuf.levels0.w + 0.5 * ubuf.levels1.x +
                          ubuf.levels1.w, 0.0, 1.0);
    float ambient = 0.2 + 0.8 * harmony;
    vec3 hue = palette(0.0);
    vec3 complement = palette(0.5);

    vec3 col = vec3(0.0);
    vec3 fogCol = hue * 0.02 * ambient + C_ATMOS * (0.1 * ubuf.atmos.x + 0.25 * ubuf.atmos.y);
    if (hit) {
        vec3 p = ro + rd * t;
        vec3 n = normalAt(p);
        float ao = occlusion(p, n);
        // One hue at a low value; the trap varies the value, not the colour.
        vec3 base = mix(hue * hue, hue, 0.5) * (0.25 + 0.6 * trap);
        float cityW = 1.0 - clamp(min(abs(ubuf.morph0.x), abs(ubuf.morph0.x - 5.0)), 0.0, 1.0);
        float lane = clamp(floor(abs(p.x)) - 1.0, 0.0, 7.0);
        float side = 1.0 - abs(n.y);
        float inLane = cityW * step(1.0, abs(p.x));
        // Lights: the chord's from the camera, a cool key light, the lead's
        // amber, the arp's orbs as they pass.
        vec3 l1 = normalize(ro + vec3(0.0, 0.6, 0.0) - p);
        vec3 l3 = normalize(vec3(0.4, 0.8, 0.3));
        vec3 refl = reflect(rd, n);
        float dif = max(dot(n, l1), 0.0);
        float dif3 = max(dot(n, l3), 0.0);
        float spec = pow(max(dot(refl, l1), 0.0), 32.0);
        float fres = pow(1.0 - max(dot(n, -rd), 0.0), 4.0);
        col = base * (0.05 + 0.85 * dif + 0.35 * dif3) * ambient * ao;
        // The crystals are glass: hard highlights and a bright rim.
        float crystalW = 1.0 - clamp(abs(ubuf.morph0.x - 2.0), 0.0, 1.0);
        col += hue * spec * (0.5 + 2.5 * crystalW) * ambient * ao;
        col += complement * fres * crystalW * 0.8 * ambient;
        col += complement * fres * (0.12 * ambient + 0.8 * ubuf.chord.y) * ao;
        vec3 ld = leadPos - p;
        col += C_LEAD * max(dot(n, normalize(ld)), 0.0) / (1.0 + 1.5 * dot(ld, ld)) * ubuf.lead.x *
               (1.5 + 2.0 * ubuf.lead.z);
        vec3 a0 = o0 - p, a1 = o1 - p, a2 = o2 - p, a3 = o3 - p;
        col += C_ARP * (e0 * max(dot(n, normalize(a0)), 0.0) / (1.0 + 4.0 * dot(a0, a0)) +
                        e1 * max(dot(n, normalize(a1)), 0.0) / (1.0 + 4.0 * dot(a1, a1)) +
                        e2 * max(dot(n, normalize(a2)), 0.0) / (1.0 + 4.0 * dot(a2, a2)) +
                        e3 * max(dot(n, normalize(a3)), 0.0) / (1.0 + 4.0 * dot(a3, a3))) * 2.5;
        // The city: each lane's windows lit by its part, the road's lines and
        // the hats' traffic streaking down it.
        vec2 win = fract(vec2(p.z * 6.0 + p.x * 6.0, p.y * 5.0));
        float lit = step(0.35, win.x) * step(0.4, win.y) * step(0.45, hash3(floor(p * vec3(6.0, 5.0, 6.0))));
        col += laneColour(lane) * lit * side * (0.04 + 2.2 * level(lane) * level(lane)) * inLane;
        float roadW = cityW * step(abs(p.x), 1.0) * step(0.9, n.y);
        float streak = smoothstep(0.75, 1.0, fract(p.z * 0.35 + time * 3.5 + hash1(floor(p.x * 4.0)) * 0.6));
        col += vec3(1.0, 0.85, 0.7) * streak * step(0.86, abs(fract(p.x * 2.0) - 0.5) * 2.0 + 0.6) * roadW *
               (0.08 + 2.5 * ubuf.drums.z);
        // Hats glint off the edges, everywhere.
        float g = hash3(floor(p * 18.0) + floor(time * 30.0));
        col += vec3(1.0) * ubuf.drums.z * step(0.992 - 0.03 * fres, g) * 3.0 * exp(-t * 0.25);
        // The fog swallows what is far.
        float fog = 1.0 - exp(-t * (0.04 + 0.08 * ubuf.atmos.x));
        col = mix(col, fogCol, fog);
    } else {
        col = fogCol;
    }
    // Edges: the chord's neon at rest, the drums' orange flaring on a kick.
    col += glow * (hue * (0.4 + 2.0 * ubuf.chord.y) * ambient + C_DRUMS * 5.0 * kick * kick * exp(-t * 0.08) + vec3(0.6) * ubuf.morph3.y);
    col += emit;
    // A build whitens everything toward the drop.
    col = mix(col, vec3(dot(col, vec3(0.33))) * 1.4 + build * 0.15, build * 0.6);
    fragColor = vec4(col, 1.0) * ubuf.qt_Opacity;
}
