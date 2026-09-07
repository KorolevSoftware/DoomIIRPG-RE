#include "render/api/QuadUV.h"

namespace newcore {

void quadCorners(int rotateMode, float halfW, float halfH,
	float u0, float v0, float u1, float v1,
	float posOut[4][2], float uvOut[4][2]) {
	const float hw = halfW;
	const float hh = halfH;

	uvOut[0][0] = u0; uvOut[0][1] = v0;
	uvOut[1][0] = u1; uvOut[1][1] = v0;
	uvOut[2][0] = u1; uvOut[2][1] = v1;
	uvOut[3][0] = u0; uvOut[3][1] = v1;

	float* pos = &posOut[0][0];
	switch (rotateMode) {
		case 1: // glRotatef(90)
			pos[0] = hh; pos[1] = -hw; pos[2] = hh; pos[3] = hw;
			pos[4] = -hh; pos[5] = hw; pos[6] = -hh; pos[7] = -hw; break;
		case 2: // glRotatef(180)
			pos[0] = hw; pos[1] = hh; pos[2] = -hw; pos[3] = hh;
			pos[4] = -hw; pos[5] = -hh; pos[6] = hw; pos[7] = -hh; break;
		case 3: // glRotatef(270)
			pos[0] = -hh; pos[1] = hw; pos[2] = -hh; pos[3] = -hw;
			pos[4] = hh; pos[5] = -hw; pos[6] = hh; pos[7] = hw; break;
		case 4: // glScalef(-1,1)
			pos[0] = hw; pos[1] = -hh; pos[2] = -hw; pos[3] = -hh;
			pos[4] = -hw; pos[5] = hh; pos[6] = hw; pos[7] = hh; break;
		case 5: // glRotatef(90); glScalef(-1,1)
			pos[0] = hh; pos[1] = hw; pos[2] = hh; pos[3] = -hw;
			pos[4] = -hh; pos[5] = -hw; pos[6] = -hh; pos[7] = hw; break;
		case 6: // glRotatef(180); glScalef(-1,1)  == mirror Y
			pos[0] = -hw; pos[1] = hh; pos[2] = hw; pos[3] = hh;
			pos[4] = hw; pos[5] = -hh; pos[6] = -hw; pos[7] = -hh; break;
		case 7: // glRotatef(270); glScalef(-1,1)
			pos[0] = -hh; pos[1] = -hw; pos[2] = -hh; pos[3] = hw;
			pos[4] = hh; pos[5] = hw; pos[6] = hh; pos[7] = -hw; break;
		case 8: // glScalef(1,-1)  == mirror Y
			pos[0] = -hw; pos[1] = hh; pos[2] = hw; pos[3] = hh;
			pos[4] = hw; pos[5] = -hh; pos[6] = -hw; pos[7] = -hh; break;
		case 0:
		default:
			pos[0] = -hw; pos[1] = -hh; pos[2] = hw; pos[3] = -hh;
			pos[4] = hw; pos[5] = hh; pos[6] = -hw; pos[7] = hh; break;
	}
}

} // namespace newcore
