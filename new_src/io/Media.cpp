#include "io/Media.h"

#include "io/DataReader.h"

#include <cstdio>
#include <functional>

namespace newcore {

bool MediaMappings::load(const std::vector<uint8_t>& data) {
	DataReader r(data);
	if (data.size() < 2 * kMaxMappings + kMaxMedia + 4 * kMaxMedia * 2 + 2 * 4 * kMaxMedia)
		return false;

	mappings.resize(kMaxMappings);
	for (int i = 0; i < kMaxMappings; ++i) mappings[i] = r.readShort();

	dimensions.resize(kMaxMedia);
	for (int i = 0; i < kMaxMedia; ++i) dimensions[i] = r.readByte();

	bounds.resize(kMaxMedia * 4);
	for (int i = 0; i < kMaxMedia * 4; ++i) bounds[i] = r.readShort();

	palColors.resize(kMaxMedia);
	for (int i = 0; i < kMaxMedia; ++i) palColors[i] = r.readInt();

	texelSizes.resize(kMaxMedia);
	for (int i = 0; i < kMaxMedia; ++i) texelSizes[i] = r.readInt();

	return true;
}

bool MediaLoader::loadMappings(const std::vector<uint8_t>& data) {
	palettes_.clear();
	texels_.clear();
	paletteIndex_.assign(MediaMappings::kMaxMedia, -1);
	texelIndex_.assign(MediaMappings::kMaxMedia, -1);
	return mappings_.load(data);
}

void MediaLoader::registerMedia(const std::vector<uint16_t>& mediaIds) {
	const auto& m = mappings_.mappings;
	constexpr int32_t kRegistered = 0x40000000;
	constexpr int32_t kReference = 0x80000000;

	for (uint16_t mediaId : mediaIds) {
		if (mediaId + 1 >= (int)m.size() || mediaId >= (int)m.size()) continue;
		int begin = m[mediaId];
		int end = m[mediaId + 1];
		if (begin < 0) begin = 0;
		if (end < 0) end = begin;

		for (int i = begin; i < end; ++i) {
			int palIndex = i;
			if (mappings_.palColors[palIndex] & kReference)
				palIndex = mappings_.palColors[palIndex] & 0x3FF;
			if (palIndex >= 0 && palIndex < MediaMappings::kMaxMedia)
				mappings_.palColors[palIndex] |= kRegistered;

			int texIndex = i;
			if (mappings_.texelSizes[texIndex] & kReference)
				texIndex = mappings_.texelSizes[texIndex] & 0x3FF;
			if (texIndex >= 0 && texIndex < MediaMappings::kMaxMedia)
				mappings_.texelSizes[texIndex] |= kRegistered;
		}
	}
}

bool MediaLoader::finalize(const std::function<std::vector<uint8_t>(const std::string&)>& open) {
	constexpr int32_t kRegistered = 0x40000000;
	constexpr int32_t kReference = 0x80000000;
	constexpr int32_t kMask = 0x3FFFFFFF;

	// ---- Palettes (newPalettes.bin) ----
	// Faithful port of python load_palettes: for every non-reference media
	// entry, read `size` RGB565 entries + 4-byte marker; pad to 256 with
	// transparent black. `paletteIndex_` = sequential store index.
	std::vector<uint8_t> palData = open("newPalettes.bin");
	if (palData.empty()) return false;
	DataReader pr(palData);

	palettes_.clear();
	paletteIndex_.assign(MediaMappings::kMaxMedia, -1);
	for (int j = 0; j < MediaMappings::kMaxMedia; ++j) {
		int32_t v = mappings_.palColors[j];
		bool reference = (v & kReference) != 0;
		if (reference) continue;

		int size = v & kMask;
		MediaPalette pal;
		pal.colors.resize(256, 0);
		for (int c = 0; c < size && !pr.eof(); ++c)
			pal.colors[c] = (uint16_t)pr.readShort();
		pr.skip(4); // trailing marker

		paletteIndex_[j] = (int)palettes_.size();
		palettes_.push_back(std::move(pal));
	}

	// ---- Texels (newTexelsXXX.bin) ----
	// Faithful port of python tools/extract_textures.py load_texels / legacy
	// finalizeMapMedia: for every non-reference media entry, read `size` bytes
	// at the running position, advance by size+4 (4-byte marker), and switch
	// to the next newTexels file whenever the position exceeds 0x40000.
	// `texelIndex_` records the sequential store index for each non-reference
	// media id (this is the same `direct_index` the python tool uses).
	texels_.clear();
	texelIndex_.assign(MediaMappings::kMaxMedia, -1);
	int fileIndex = 0;      // current newTexels file (python `m`)
	int loadedFile = -1;    // file already loaded (python `loaded_m`)
	int filePos = 0;        // position in current file (python `n12`)
	std::vector<uint8_t> texData;

	for (int k = 0; k < MediaMappings::kMaxMedia; ++k) {
		int32_t v = mappings_.texelSizes[k];
		bool reference = (v & kReference) != 0;
		int size = (v & kMask) + 1;

		if (!reference) {
			if (fileIndex != loadedFile) {
				std::string name = "newTexels" +
					std::string(3 - std::to_string(fileIndex).size(), '0') +
					std::to_string(fileIndex) + ".bin";
				texData = open(name);
				loadedFile = fileIndex;
				filePos = 0;
			}

			MediaTexel tex;
			int bytes = size;
			if (filePos + bytes > (int)texData.size()) bytes = (int)texData.size() - filePos;
			if (bytes < 0) bytes = 0;
			if (bytes > 0)
				tex.data.assign(texData.begin() + filePos, texData.begin() + filePos + bytes);
			// dimensions nibbles are *bit* counts for width/height (legacy:
			// v8 = dims&0xF -> height = 1<<v8; v10 = (dims>>4)&0xF -> width = 1<<v10).
			tex.width = (int)(1 << ((mappings_.dimensions[k] >> 4) & 0x0F));
			tex.height = (int)(1 << (mappings_.dimensions[k] & 0x0F));
			texelIndex_[k] = (int)texels_.size();
			texels_.push_back(std::move(tex));

			filePos += size + 4;
		}

		// File-split check is unconditional (matches python / legacy C++).
		if (filePos > 0x40000) {
			++fileIndex;
			filePos = 0;
		}
	}

	// Resolve MEDIA_FLAG_REFERENCE entries: a referencing media id carries no
	// data of its own; it points at another entry (legacy finalizeMapMedia
	// rewrites references to point at the loaded slot so that use-time masking
	// yields a valid index for every id). Port: alias the source entry's store
	// index. Sources always precede or equal their referencers in practice, but
	// running this as a separate pass after loading makes order irrelevant for
	// single-hop references.
	for (int k = 0; k < MediaMappings::kMaxMedia; ++k) {
		int32_t p = mappings_.palColors[k];
		if ((p & kReference) != 0 && paletteIndex_[k] < 0) {
			int src = p & kMask;
			if (src >= 0 && src < MediaMappings::kMaxMedia && !(mappings_.palColors[src] & kReference))
				paletteIndex_[k] = paletteIndex_[src];
		}
		int32_t t = mappings_.texelSizes[k];
		if ((t & kReference) != 0 && texelIndex_[k] < 0) {
			int src = t & kMask;
			if (src >= 0 && src < MediaMappings::kMaxMedia && !(mappings_.texelSizes[src] & kReference))
				texelIndex_[k] = texelIndex_[src];
		}
	}

	return true;
}

int MediaLoader::paletteIndexFor(int mediaId) const {
	if (mediaId < 0 || mediaId >= (int)paletteIndex_.size()) return -1;
	return paletteIndex_[mediaId];
}

int MediaLoader::texelIndexFor(int mediaId) const {
	if (mediaId < 0 || mediaId >= (int)texelIndex_.size()) return -1;
	return texelIndex_[mediaId];
}

uint8_t MediaLoader::mediaWidth(int mediaId) const {
	if (mediaId < 0 || mediaId >= (int)mappings_.dimensions.size()) return 0;
	return mappings_.dimensions[mediaId] & 0x0F;
}

uint8_t MediaLoader::mediaHeight(int mediaId) const {
	if (mediaId < 0 || mediaId >= (int)mappings_.dimensions.size()) return 0;
	return mappings_.dimensions[mediaId] >> 4;
}

void MediaLoader::clear() {
	palettes_.clear();
	texels_.clear();
	paletteIndex_.clear();
	texelIndex_.clear();
}

} // namespace newcore