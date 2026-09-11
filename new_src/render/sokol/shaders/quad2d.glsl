// The 2D layer: three programs on one vertex shader, a port of the inline GLSL
// in render/gl/GlDraw2D.cpp:15-76. Vertex layout is unchanged: vec2 pos,
// vec2 uv0, vec4 color0 (32 bytes, GlDraw2D::Vertex).
// Compiled by sokol-shdc into ${CMAKE_CURRENT_BINARY_DIR}/compile_shaders/.

@vs vs2d
layout(binding=0) uniform vs2d_params {
    vec4 canvas_size;   // .xy = canvas pixels (480,320); .zw unused
};

in vec2 pos;
in vec2 uv0;
in vec4 color0;

out vec2 uv;
out vec4 color;

void main() {
    uv = uv0;
    color = color0;
    // Canvas origin is top-left, clip space +Y is up: flip Y.
    float nx = pos.x / canvas_size.x * 2.0 - 1.0;
    float ny = 1.0 - pos.y / canvas_size.y * 2.0;
    // z = 0, w = 1 is inside both the GL and the Metal/D3D11 clip volume, so
    // the 2D layer needs no depth remap (spec 2026-09-11 §0.4).
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
@end

@fs fs2d_indexed
layout(binding=1) uniform fs2d_params {
    vec4 color_mod;
};
layout(binding=0) uniform texture2D tex;   // R8 index texture
layout(binding=1) uniform texture2D pal;   // RGBA8 palette LUT, 256x1
layout(binding=0) uniform sampler smp;
// Own sampler for the LUT, always clamping — see the note in world.glsl.
layout(binding=1) uniform sampler smp_pal;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    float index = texture(sampler2D(tex, smp), uv).r;
    frag_color = texture(sampler2D(pal, smp_pal), vec2(index, 0.5)) * color * color_mod;
}
@end

@fs fs2d_rgba
layout(binding=1) uniform fs2d_params {
    vec4 color_mod;
};
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv) * color * color_mod;
}
@end

@fs fs2d_color
layout(binding=1) uniform fs2d_params {
    vec4 color_mod;
};

in vec2 uv;
in vec4 color;
out vec4 frag_color;

void main() {
    // fillQuad samples nothing: the 1x1 white texture of GlDraw2D.cpp:118-128
    // has no successor here.
    frag_color = color * color_mod;
}
@end

@program quad_indexed vs2d fs2d_indexed
@program quad_rgba vs2d fs2d_rgba
@program quad_color vs2d fs2d_color
