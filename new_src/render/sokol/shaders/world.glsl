// The world program, a port of the inline GLSL in render/gl/GlScene3D.cpp:15-56.
// Vertex layout is unchanged: vec3 pos, vec2 uv0 (20 bytes, newcore::WorldVertex).
// Compiled by sokol-shdc into ${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/.

@vs vs_world
layout(binding=0) uniform vs_world_params {
    mat4 mvp;
    mat4 view;        // eye-space depth for the fog
    vec4 depth_fix;   // .x/.y remap clip z; (1,0) on GL, (0.5,0.5) on Metal/D3D11
};

in vec3 pos;
in vec2 uv0;

out vec2 uv;
out float fog_depth;

void main() {
    uv = uv0;
    vec4 eye = view * vec4(pos, 1.0);
    fog_depth = -eye.z;
    vec4 p = mvp * vec4(pos, 1.0);
    // Camera3D emits a GL-style projection (clip -w <= z <= w); Metal and D3D11
    // clip 0 <= z <= w. depth_fix maps the one onto the other and is the exact
    // identity on GL (spec 2026-09-11 §0.4).
    p.z = p.z * depth_fix.x + p.w * depth_fix.y;
    gl_Position = p;
}
@end

@fs fs_world
layout(binding=1) uniform fs_world_params {
    vec4 color_mod;
    vec4 fog_color;    // mode 1: legacy fog ARGB bytes (r,g,b,a) 0..255; mode 2: flat rgb 0..1
    vec4 fog_params;   // .x fogMin, .y fogRange, .z mode 0/1/2, .w forced z (0 = from fog_depth)
};
layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D pal;
layout(binding=0) uniform sampler smp;
// The palette LUT needs its own sampler: GlTexture gives the index texture
// GL_REPEAT for tiled world textures but always clamps the palette
// (GlTexture.cpp:50-56, :73-76). Sampling the LUT with REPEAT wraps index 255
// (u = 1.0) back to entry 0, which is a wrong color, not a rounding error.
layout(binding=1) uniform sampler smp_pal;

in vec2 uv;
in float fog_depth;
out vec4 frag_color;

// C integer division (truncates toward zero) for possibly negative operands.
int cdiv(int a, int b) {
    int q = abs(a) / abs(b);
    return ((a < 0) != (b < 0)) ? -q : q;
}

void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    vec4 col = texture(sampler2D(pal, smp_pal), vec2(index, 0.5));
    if (fog_params.z == 2.0) {
        col.rgb = fog_color.rgb;                 // RENDER_FLAG_PULSATE flat grey (ADR 0048)
    } else if (fog_params.z == 1.0 && col.a != 0.0) {
        // TinyGL software fog, ADR 0049. w = fog_depth * 16384 (script-fog.md §3).
        int z = fog_params.w > 0.0 ? int(fog_params.w)
              : int(8388607.0 / max(fog_depth * 16384.0, 1.0));  // projectVerts, TinyGL.cpp:521
        int d = 8388607 / max(z, 1);                              // getFogPalette, TinyGL.cpp:48
        int lvl = clamp(cdiv((d - int(fog_params.x)) * 16, int(fog_params.y)), 0, 15);
        if (lvl > 0) {
            int n2 = ((lvl << 8) / 16 * int(fog_color.a)) >> 8;   // Render.cpp:1864
            int f = 256 - n2;
            ivec3 c8 = ivec3(col.rgb * 255.0 + 0.5);           // LUT = 565 bit-replicated
            int dat = ((c8.r >> 3) << 11) | ((c8.g >> 2) << 5) | (c8.b >> 3);
            int sc = (((f >> 2) * ((dat & 0x07E0) >> 6)) & 0x07E0)
                   | ((((f >> 3) * (dat & 0xF81F)) >> 5) & 0xF81F); // buildFogTable, Render.cpp:1830
            ivec3 fc = (ivec3(fog_color.rgb) * n2) >> 8;
            int o = (sc + (((fc.r >> 3) << 11) | ((fc.g >> 2) << 5) | (fc.b >> 3))) & 0xFFFF;
            int r5 = (o >> 11) & 31; int g6 = (o >> 5) & 63; int b5 = o & 31;
            col.rgb = vec3(float((r5 << 3) | (r5 >> 2)), float((g6 << 2) | (g6 >> 4)),
                           float((b5 << 3) | (b5 >> 2))) / 255.0;
        }
    }
    col *= color_mod;   // setupPalette render-mode transform comes after the fog palette
    frag_color = col;
}
@end

@program world vs_world fs_world
