#include "platform/FileSystem.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

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

bool FileSystem::ensureSaveDirectory() {
	struct stat st;
	if (stat(savePath_.c_str(), &st) == 0) return true;
	return mkdir(savePath_.c_str(), 0777) == 0;
}

bool FileSystem::readFile(const std::string& path, std::vector<uint8_t>& out) {
	FILE* fp = std::fopen(path.c_str(), "rb");
	if (!fp) return false;
	std::fseek(fp, 0, SEEK_END);
	long size = std::ftell(fp);
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

bool FileSystem::writeFile(const std::string& path, const void* data, size_t size) {
	std::string tmp = path + ".tmp";
	FILE* fp = std::fopen(tmp.c_str(), "wb");
	if (!fp) return false;
	if (size > 0) {
		if (std::fwrite(data, 1, size, fp) != size) {
			std::fclose(fp);
			std::remove(tmp.c_str());
			return false;
		}
	}
	std::fclose(fp);
	if (std::rename(tmp.c_str(), path.c_str()) != 0) {
		std::remove(tmp.c_str());
		return false;
	}
	return true;
}

} // namespace newcore
