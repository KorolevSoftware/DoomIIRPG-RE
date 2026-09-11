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
    vec4 fog_color;
    vec4 fog_params;   // .x = start, .y = end, .z = enabled (0/1), .w unused
};
layout(binding=0) uniform texture2D tex;
layout(binding=1) uniform texture2D pal;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in float fog_depth;
out vec4 frag_color;

void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    vec4 col = texture(sampler2D(pal, smp), vec2(index, 0.5));
    // Fixed-pipeline GL_MODULATE (src/GLES.cpp:620), BEFORE fog, as ADR 0019 requires.
    col *= color_mod;
    if (fog_params.z != 0.0) {
        // Fog touches RGB only, so transparent billboard texels stay transparent.
        float f = clamp((fog_params.y - fog_depth) /
                        max(fog_params.y - fog_params.x, 1e-6), 0.0, 1.0);
        col.rgb = mix(fog_color.rgb, col.rgb, f);
    }
    frag_color = col;
}
@end

@program world vs_world fs_world
