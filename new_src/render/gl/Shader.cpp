#include "render/gl/Shader.h"

#include <cstdio>
#include <vector>

namespace newcore {

Shader::~Shader() {
	if (program_) glDeleteProgram(program_);
}

Shader::Shader(Shader&& other) noexcept : program_(other.program_) {
	other.program_ = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
	if (this != &other) {
		if (program_) glDeleteProgram(program_);
		program_ = other.program_;
		other.program_ = 0;
	}
	return *this;
}

GLuint Shader::compileShader(GLenum type, const char* source, std::string* error) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		GLint length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(length > 1 ? length : 1);
		glGetShaderInfoLog(shader, (GLsizei)log.size(), nullptr, log.data());
		if (error) *error = std::string(log.data());
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

bool Shader::compile(const char* vertexSource, const char* fragmentSource, std::string* error) {
	GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, error);
	if (!vs) return false;
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, error);
	if (!fs) {
		glDeleteShader(vs);
		return false;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		GLint length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
		std::vector<char> log(length > 1 ? length : 1);
		glGetProgramInfoLog(program, (GLsizei)log.size(), nullptr, log.data());
		if (error) *error = std::string(log.data());
		glDeleteProgram(program);
		return false;
	}

	if (program_) glDeleteProgram(program_);
	program_ = program;
	return true;
}

void Shader::use() const {
	glUseProgram(program_);
}

GLint Shader::uniform(const char* name) const {
	return glGetUniformLocation(program_, name);
}

void Shader::setInt(const char* name, int v) const {
	glUniform1i(uniform(name), v);
}

void Shader::setFloat(const char* name, float v) const {
	glUniform1f(uniform(name), v);
}

void Shader::setVec2(const char* name, float x, float y) const {
	glUniform2f(uniform(name), x, y);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
	glUniform4f(uniform(name), x, y, z, w);
}

void Shader::setMat4(const char* name, const float* m) const {
	glUniformMatrix4fv(uniform(name), 1, GL_FALSE, m);
}

} // namespace newcore