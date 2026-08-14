#ifndef NEW_IO_MEDIA_H
#define NEW_IO_MEDIA_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace newcore {

// Raw media tables parsed from newMappings.bin (game textures/palettes).
struct MediaMappings {
	static constexpr int kMaxMedia = 1024;
	static constexpr int kMaxMappings = 512;

	std::vector<int16_t> mappings;   // kMaxMappings shorts
	std::vector<uint8_t> dimensions; // kMaxMedia bytes  (low nibble w, high nibble h)
	std::vector<int16_t> bounds;     // kMaxMedia * 4 shorts
	std::vector<int32_t> palColors;  // kMaxMedia ints
	std::vector<int32_t> texelSizes; // kMaxMedia ints

	bool load(const std::vector<uint8_t>& data);
};

// A single 256-entry RGB565 palette as loaded from newPalettes.bin.
struct MediaPalette {
	std::vector<uint16_t> colors; // size 256
};

// A single indexed texel block (each byte is a palette index).
struct MediaTexel {
	std::vector<uint8_t> data;
	int width = 0;
	int height = 0;
};

// Loads palette/texel data for the set of media ids used by a map.
// Mirrors the legacy LoadingManager registerMapMedia/finalizeMapMedia flow.
class MediaLoader {
public:
	// Parses newMappings.bin; must be called once at startup.
	bool loadMappings(const std::vector<uint8_t>& data);

	// Marks the palettes/texels referenced by a map's media list as used.
	// mediaIds: array of media ids from the map file.
	void registerMedia(const std::vector<uint16_t>& mediaIds);

	// Loads palettes and texels for all registered media from the given
	// file accessor. file accessor must return the file bytes or empty.
	bool finalize(const std::function<std::vector<uint8_t>(const std::string&)>& open);

	// Result accessors.
	const MediaPalette& palette(int index) const { return palettes_[index]; }
	int paletteCount() const { return static_cast<int>(palettes_.size()); }
	const MediaTexel& texel(int index) const { return texels_[index]; }
	int texelCount() const { return static_cast<int>(texels_.size()); }

	// Maps a media id to the palette/texel index, or -1.
	int paletteIndexFor(int mediaId) const;
	int texelIndexFor(int mediaId) const;

	// Raw dimensions for a media id (0 if none).
	uint8_t mediaWidth(int mediaId) const;
	uint8_t mediaHeight(int mediaId) const;

	const MediaMappings& mappings() const { return mappings_; }

	void clear();

private:
	MediaMappings mappings_;
	std::vector<MediaPalette> palettes_;
	std::vector<MediaTexel> texels_;
	std::vector<int> paletteIndex_; // kMaxMedia entries
	std::vector<int> texelIndex_;   // kMaxMedia entries
};

} // namespace newcore

#endif // NEW_IO_MEDIA_H