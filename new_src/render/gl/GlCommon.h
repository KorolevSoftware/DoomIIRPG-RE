#ifndef NEW_CORE_GL_COMMON_H
#define NEW_CORE_GL_COMMON_H

#ifdef __APPLE__
	#include <OpenGL/gl3.h>
	#include <OpenGL/gl3ext.h>
#elif defined(_WIN32)
	#include <GL/gl.h>
#else
	#include <GL/gl.h>
#endif

#endif // NEW_CORE_GL_COMMON_H
