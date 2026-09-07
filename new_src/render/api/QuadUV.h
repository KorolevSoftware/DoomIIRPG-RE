#ifndef NEW_RENDER_API_QUADUV_H
#define NEW_RENDER_API_QUADUV_H

namespace newcore {

// Legacy rotateMode 0..8 -> one quad, extracted verbatim from
// the GL quad builder (now render/gl/GlDraw2D.cpp, GlDraw2D::drawQuad) so the
// backends cannot drift. Each mode replicates legacy Image::DrawTexture's glRotatef/glScalef
// around the quad center: the *geometry* rotates, which is why the odd modes
// (90/270 and their mirrors) swap halfW/halfH instead of just permuting UVs.
//
// posOut holds the four corner offsets relative to the destination rect center,
// uvOut the matching texture coordinates. Order is the source quad's
// top-left, top-right, bottom-right, bottom-left.
void quadCorners(int rotateMode, float halfW, float halfH,
	float u0, float v0, float u1, float v1,
	float posOut[4][2], float uvOut[4][2]);

} // namespace newcore

#endif // NEW_RENDER_API_QUADUV_H
