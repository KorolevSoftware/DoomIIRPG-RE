#ifndef NEW_PLATFORM_FILESYSTEM_H
#define NEW_PLATFORM_FILESYSTEM_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace newcore {

// Path helpers for locating the data archive and user files.
class FileSystem {
public:
	static FileSystem& instance();

	// Directory that contains the executable / game data.
	const std::string& basePath() const { return basePath_; }

	// Directory where save games live ("Doom2rpg.app").
	const std::string& savePath() const { return savePath_; }

	// Resolves the game data archive path. Searches the base path then the
	// current working directory.
	std::string findDataArchive(const std::string& archiveName) const;

	bool ensureSaveDirectory();

	// Reads an entire file into memory. Returns false on failure or when the
	// file is larger than maxSize.
	static bool readFile(const std::string& path, std::vector<uint8_t>& out,
		size_t maxSize = SIZE_MAX);

	static bool fileExists(const std::string& path);

	// True if the file was removed or did not exist.
	static bool removeFile(const std::string& path);

	// rename(); replaces an existing destination file.
	static bool moveFile(const std::string& from, const std::string& to);

	// mkdir unless it exists.
	static bool ensureDirectory(const std::string& path);

	// Writes a file atomically (temp + rename). Returns false on failure.
	static bool writeFile(const std::string& path, const void* data, size_t size);

private:
	FileSystem();
	void initialize();

	std::string basePath_;
	std::string savePath_;
};

} // namespace newcore

#endif // NEW_PLATFORM_FILESYSTEM_H
