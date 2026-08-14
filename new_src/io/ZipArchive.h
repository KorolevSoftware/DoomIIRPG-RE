#ifndef NEW_IO_ZIPARCHIVE_H
#define NEW_IO_ZIPARCHIVE_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace newcore {

// Minimal zip reader supporting stored and deflate entries. Used to access the
// game data archive ("Doom 2 RPG.ipa").
class ZipArchive {
public:
	ZipArchive() = default;
	~ZipArchive() = default;

	ZipArchive(const ZipArchive&) = delete;
	ZipArchive& operator=(const ZipArchive&) = delete;

	// Opens the archive file and parses the central directory.
	bool open(const std::string& path);
	void close();

	bool isOpen() const { return !entries_.empty(); }

	// Case-insensitive lookup of an entry by its full path.
	bool hasEntry(const std::string& name) const;

	// Extracts an entry's uncompressed contents. Returns false if missing.
	bool readEntry(const std::string& name, std::vector<uint8_t>& out) const;

	int entryCount() const { return static_cast<int>(entries_.size()); }

private:
	struct Entry {
		uint32_t offset = 0;
		uint32_t compressedSize = 0;
		uint32_t uncompressedSize = 0;
		uint16_t method = 0;
	};

	static std::string normalize(const std::string& name);

	std::vector<uint8_t> fileData_;
	std::unordered_map<std::string, Entry> entries_;
};

} // namespace newcore

#endif // NEW_IO_ZIPARCHIVE_H
