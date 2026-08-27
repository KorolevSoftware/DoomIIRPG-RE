#ifndef NEW_RENDER_CAMERA3D_H
#define NEW_RENDER_CAMERA3D_H

#include <cstdint>

namespace newcore {

// 3D camera for the world renderer. Faithful port of the legacy math
// (TinyGL::buildViewMatrix / buildProjectionMatrix / multMatrix, 14.14
// fixed-point) including the GLES BeginFrame projection adjustments, so the
// produced MVP matches the original GL path. Coordinates are in world units
// (vertex byte << 7, map tile grid 32x32 = 2048 units).
//
// The caller passes the combined MVP (float[16], column-major as used by
// glUniformMatrix4fv) to the world shader, plus vertices scaled by
// VERT_COORDS_TO_FLOAT (divide by 16384) and UVs by TEXT_COORDS_TO_FLOAT
// (divide by 1024), exactly like the legacy DrawModelVerts.
class Camera3D {
public:
	Camera3D();

	// sinTable: 1024-entry fixed-point sine table (legacy/TinyGL format,
	// sinTable[i] ~ sin(i * 2pi / 1024) << 14).
	void setSinTable(const int32_t* sinTable) { sinTable_ = sinTable; }
	const int32_t* sinTable() const { return sinTable_; }

	// Builds the combined MVP matrix from a camera.
	// viewAspect = (viewFov<<14) / ((viewportW<<14)/viewportH), like legacy.
	// chatZoom mirrors Render::chatZoom (affects projection[14] *= 0.5).
	void setView(int viewX, int viewY, int viewZ, int viewYaw, int viewPitch,
		int viewRoll, int viewFov, int viewAspect, bool chatZoom = false);

	// Combined MVP as float[16] column-major (ready for glUniformMatrix4fv).
	const float* mvp() const { return mvpF_; }

	// Raw 14.14 view matrix (used for billboard placement, e.g. viewMtxMove).
	const int* viewInt() const { return view_; }

	// View matrix as float[16] (14.14 -> /16384), for shader eye-space depth.
	const float* viewFloat() const { return viewF_; }

	// Raw 14.14 combined MVP (used for sprite depth sorting like legacy).
	const int* mvpInt() const { return mvp_; }

	// Raw 14.14 projection matrix, post GLES BeginFrame adjustments
	// ([1],[5] negated, Camera3D.cpp:85-89). The view-weapon billboard
	// magnification is derived from [0]/[5] (src/GLES.cpp:483-547).
	const int* projectionInt() const { return projection_; }

	// Cached view angles (masked 0x3FF) and position.
	int viewX() const { return viewX_; }
	int viewY() const { return viewY_; }
	int viewZ() const { return viewZ_; }
	int viewYaw() const { return viewYaw_; }
	int viewPitch() const { return viewPitch_; }

private:
	void buildViewMatrix(int x, int y, int z, int yaw, int pitch, int roll, int* matrix);
	void buildProjectionMatrix(int fov, int aspect, int* matrix);
	void multMatrix(const int* a, const int* b, int* dest);

	const int32_t* sinTable_ = nullptr;

	int viewX_ = 0, viewY_ = 0, viewZ_ = 0;
	int viewYaw_ = 0, viewPitch_ = 0;

	int view_[16];
	int projection_[16];
	int mvp_[16];
	float mvpF_[16];
	float viewF_[16];
};

} // namespace newcore

#endif // NEW_RENDER_CAMERA3D_H