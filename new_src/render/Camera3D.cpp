#include "render/Camera3D.h"

namespace newcore {

namespace {
constexpr int kMatrixOne = 16384; // 1.0 in 14.14 fixed point
constexpr int kNearClip = 256;
}

Camera3D::Camera3D() {
	for (int i = 0; i < 16; ++i) { view_[i] = 0; projection_[i] = 0; }
	view_[15] = kMatrixOne;
}

void Camera3D::buildViewMatrix(int x, int y, int z, int yaw, int pitch, int roll, int* matrix) {
	const int* st = sinTable_;
	int iVar7 = st[(yaw + 512) & 0x3ff] >> 2;
	int iVar6 = st[(yaw + 256) & 0x3ff] >> 2;
	int iVar1 = st[(pitch + 512) & 0x3ff] >> 2;
	int iVar2 = st[(pitch + 256) & 0x3ff] >> 2;
	int iVar3 = st[roll & 0x3ff] >> 2;
	int iVar5 = st[(roll + 256) & 0x3ff] >> 2;
	int n13 = iVar3 * iVar1 >> 14;
	int n14 = iVar5 * iVar1 >> 14;
	matrix[0] = n13 * iVar6 + iVar5 * -iVar7 >> 14;
	matrix[4] = n13 * iVar7 + iVar5 * iVar6 >> 14;
	matrix[8] = iVar3 * iVar2 >> 14;
	matrix[1] = -(n14 * iVar6 + -iVar3 * -iVar7) >> 14;
	matrix[5] = -(n14 * iVar7 + -iVar3 * iVar6) >> 14;
	matrix[9] = -(iVar5 * iVar2) >> 14;
	matrix[2] = -(iVar2 * iVar6) >> 14;
	matrix[6] = -(iVar2 * iVar7) >> 14;
	matrix[10] = -(-iVar1);

	for (int i = 0; i < 3; ++i) {
		matrix[12 + i] = -(x * matrix[0 + i] + y * matrix[4 + i] + z * matrix[8 + i]) >> 14;
	}

	matrix[3] = 0;
	matrix[7] = 0;
	matrix[11] = 0;
	matrix[15] = kMatrixOne;
}

void Camera3D::buildProjectionMatrix(int fov, int aspect, int* matrix) {
	const int* st = sinTable_;
	int n3 = aspect >> 1;
	int n4 = (fov << 14) / aspect;
	int n5 = st[n3 & 0x3FF] >> 2;
	int n6 = st[(n3 + 256) & 0x3FF] >> 2;
	matrix[0] = (n6 << 14) / (n4 * n5 >> 14);
	matrix[8] = (matrix[4] = 0);
	matrix[1] = (matrix[12] = 0);
	matrix[5] = (n6 << 14) / n5;
	matrix[13] = (matrix[9] = 0);
	matrix[6] = (matrix[2] = 0);
	matrix[10] = -kMatrixOne;
	matrix[14] = -(2 * kNearClip);
	matrix[7] = (matrix[3] = 0);
	matrix[11] = -kMatrixOne;
	matrix[15] = 0;
}

void Camera3D::multMatrix(const int* m1, const int* m2, int* dest) {
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			dest[i * 4 + j] = (m1[i * 4 + 0] * m2[0 + j] + m1[i * 4 + 1] * m2[4 + j] + m1[i * 4 + 2] * m2[8 + j] + m1[i * 4 + 3] * m2[12 + j]) >> 14;
		}
	}
}

void Camera3D::setView(int viewX, int viewY, int viewZ, int viewYaw, int viewPitch,
	int viewRoll, int viewFov, int viewAspect, bool chatZoom) {
	viewX_ = viewX;
	viewY_ = viewY;
	viewZ_ = viewZ;
	viewYaw_ = viewYaw & 0x3ff;
	viewPitch_ = viewPitch & 0x3ff;

	buildViewMatrix(viewX, viewY, viewZ, viewYaw_, viewPitch_, viewRoll, view_);
	buildProjectionMatrix(viewFov, viewAspect, projection_);

	// GLES BeginFrame adjustments to the projection matrix (non-iPhone path):
	// negate [4],[8],[1],[5],[9]; halve [14] (z scale for depth) unless chatZoom.
	projection_[4] = -projection_[4];
	projection_[8] = -projection_[8];
	projection_[1] = -projection_[1];
	projection_[5] = -projection_[5];
	projection_[9] = -projection_[9];
	if (!chatZoom) {
		projection_[14] = projection_[14] >> 1;
	}

	// Combined MVP = view * projection (row-major 14.14), the same product the
	// legacy fixed-function GL path computes internally; converting to float
	// (divide by 16384) and passing column-major to glUniformMatrix4fv with
	// gl_Position = uMVP * pos reproduces clip = proj * modelview * vertex.
	multMatrix(view_, projection_, mvp_);
	for (int i = 0; i < 16; ++i) {
		mvpF_[i] = (float)mvp_[i] * (1.f / 16384.f);
	}
}

} // namespace newcore