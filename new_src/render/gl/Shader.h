#ifndef NEW_RENDER_GL_SHADER_H
#define NEW_RENDER_GL_SHADER_H

#include <cstdint>
#include <string>

#include "render/gl/GlCommon.h"

namespace newcore {

// Owns a compiled GLSL program. This is a modern GL 3.3 core backend;
// no fixed-function pipeline is used.
class Shader {
public:
	Shader() = default;
	~Shader();

	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;

	Shader(Shader&& other) noexcept;
	Shader& operator=(Shader&& other) noexcept;

	// Compiles vertex + fragment shaders. Returns false and fills error on failure.
	bool compile(const char* vertexSource, const char* fragmentSource, std::string* error = nullptr);

	void use() const;
	bool valid() const { return program_ != 0; }

	GLint uniform(const char* name) const;

	void setInt(const char* name, int v) const;
	void setFloat(const char* name, float v) const;
	void setVec2(const char* name, float x, float y) const;
	void setVec4(const char* name, float x, float y, float z, float w) const;
	// Sets a 4x4 matrix uniform (column-major, 16 floats).
	void setMat4(const char* name, const float* m) const;

private:
	GLuint compileShader(GLenum type, const char* source, std::string* error);

	GLuint program_ = 0;
};

} // namespace newcore

#endif // NEW_RENDER_GL_SHADER_H