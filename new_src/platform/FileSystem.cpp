#include "platform/FileSystem.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace newcore {

FileSystem& FileSystem::instance() {
	static FileSystem fs;
	return fs;
}

FileSystem::FileSystem() { initialize(); }

void FileSystem::initialize() {
	char* base = SDL_GetBasePath();
	if (base) {
		basePath_ = base;
		SDL_free(base);
	} else {
		basePath_ = "./";
	}
	savePath_ = basePath_ + "Doom2rpg.app";
}

std::string FileSystem::findDataArchive(const std::string& archiveName) const {
	std::string candidate = basePath_ + archiveName;
	struct stat st;
	if (stat(candidate.c_str(), &st) == 0) return candidate;

	candidate = archiveName;
	if (stat(candidate.c_str(), &st) == 0) return candidate;

	return std::string();
}

bool FileSystem::ensureSaveDirectory() { return ensureDirectory(savePath_); }

bool FileSystem::ensureDirectory(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) == 0) return true;
	return mkdir(path.c_str(), 0777) == 0;
}

bool FileSystem::moveFile(const std::string& from, const std::string& to) {
	return std::rename(from.c_str(), to.c_str()) == 0;
}

bool FileSystem::readFile(const std::string& path, std::vector<uint8_t>& out, size_t maxSize) {
	FILE* fp = std::fopen(path.c_str(), "rb");
	if (!fp) return false;
	std::fseek(fp, 0, SEEK_END);
	long size = std::ftell(fp);
	if (size < 0 || static_cast<unsigned long>(size) > maxSize) {
		std::fclose(fp);
		return false;
	}
	std::fseek(fp, 0, SEEK_SET);
	out.resize(static_cast<size_t>(size));
	if (size > 0) {
		if (std::fread(out.data(), 1, static_cast<size_t>(size), fp) != static_cast<size_t>(size)) {
			std::fclose(fp);
			return false;
		}
	}
	std::fclose(fp);
	return true;
}

bool FileSystem::fileExists(const std::string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

bool FileSystem::removeFile(const std::string& path) {
	return std::remove(path.c_str()) == 0 || !fileExists(path);
}

bool FileSystem::writeFile(const std::string& path, const void* data, size_t size) {
	std::string tmp = path + ".tmp";
	FILE* fp = std::fopen(tmp.c_str(), "wb");
	if (!fp) return false;
	// The temp file must be complete on disk before it may replace the old one.
	bool ok = size == 0 || std::fwrite(data, 1, size, fp) == size;
	ok = ok && std::fflush(fp) == 0 && fsync(fileno(fp)) == 0;
	ok = std::fclose(fp) == 0 && ok;
	if (!ok) {
		std::remove(tmp.c_str());
		return false;
	}
	if (std::rename(tmp.c_str(), path.c_str()) != 0) {
		std::remove(tmp.c_str());
		return false;
	}
	return true;
}

} // namespace newcore
