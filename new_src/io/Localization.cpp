#include "io/Localization.h"

#include "io/DataReader.h"

namespace newcore {

bool Localization::loadIndex(const std::vector<uint8_t>& data) {
	DataReader reader(data);
	if (data.size() < 2) return false;

	// Legacy loadFileIndex: 2-byte LE count, then 5-byte records
	// (byte chunk | int32 LE offset), delta encoded.
	int count = reader.readShort();
	textIndex_.clear();
	textIndex_.reserve(static_cast<size_t>(count));

	int pos = 2;
	int n4 = 0;
	while (n4 < count && pos + 5 <= (int)data.size()) {
		uint8_t b = data[pos];
		int32_t i = (int32_t)(data[pos + 1] | (data[pos + 2] << 8) |
			(data[pos + 3] << 16) | ((uint32_t)data[pos + 4] << 24));
		pos += 5;

		if (i != 0 && n4 > 0) {
			textIndex_[n4 - 1].length = i - textIndex_[n4 - 1].offset;
		}
		if (b != 0xFF) {
			TextEntry e;
			e.chunk = b;
			e.offset = i;
			e.length = 0;
			textIndex_.push_back(e);
			++n4;
		}
	}
	if (pos + 5 <= (int)data.size()) {
		int32_t i = (int32_t)(data[pos + 1] | (data[pos + 2] << 8) |
			(data[pos + 3] << 16) | ((uint32_t)data[pos + 4] << 24));
		if (n4 > 0) textIndex_[n4 - 1].length = i - textIndex_[n4 - 1].offset;
	}

	return n4 > 0;
}

void Localization::unloadAll() {
	for (int i = 0; i < kMaxTextTypes; ++i) {
		loaded_[i] = false;
		types_[i].strings.clear();
		types_[i].textMap.clear();
	}
	chunksLoaded_ = false;
	for (auto& c : chunks_) c.clear();
}

bool Localization::loadChunk(int chunk, const std::vector<uint8_t>& data) {
	if (chunk < 0 || chunk > 2) return false;
	chunks_[chunk] = data;
	chunksLoaded_ = true;
	return true;
}

bool Localization::loadTextType(int language, int type) {
	if (type < 0 || type >= kMaxTextTypes) return false;
	if (language < 0) language = 0;
	language_ = language;

	if (!chunksLoaded_) return false;

	int entryIndex = (type + language * kMaxTextTypes);
	if (entryIndex >= (int)textIndex_.size()) return false;

	const TextEntry& e = textIndex_[entryIndex];
	const std::vector<uint8_t>& chunk = chunks_[e.chunk];

	std::string raw;
	if (e.offset >= 0 && e.length > 0 && e.offset + e.length <= (int)chunk.size()) {
		raw.assign(reinterpret_cast<const char*>(chunk.data() + e.offset), e.length);
	}

	LoadedType& lt = types_[type];
	lt.strings.clear();
	lt.textMap.clear();

	// Build the text map: offsets of NUL-terminated strings.
	std::vector<uint16_t> map;
	std::vector<std::string> strings;
	map.push_back(0);
	size_t start = 0;
	for (size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] == 0) {
			strings.push_back(raw.substr(start, i - start));
			map.push_back(static_cast<uint16_t>(i + 1));
			start = i + 1;
		}
	}
	if (start < raw.size()) {
		strings.push_back(raw.substr(start));
	}

	lt.strings = std::move(strings);
	lt.textMap = std::move(map);
	loaded_[type] = true;
	return true;
}

void Localization::setLanguage(int language) {
	language_ = language;
	unloadAll();
	// Reload cached chunks (already in memory), then reload loaded types.
	// Types that were loaded remain loaded; caller must re-load them.
}

bool Localization::isLoaded(int type) const {
	return type >= 0 && type < kMaxTextTypes && loaded_[type];
}

std::string Localization::get(int type, int index) const {
	if (type < 0 || type >= kMaxTextTypes) return {};
	if (!loaded_[type]) return {};
	const LoadedType& lt = types_[type];
	if (index < 0 || index >= (int)lt.strings.size()) return {};
	return lt.strings[index];
}

std::string Localization::titleOf(const std::string& s) {
	size_t pos = s.find('|');
	if (pos == std::string::npos) return s;
	return s.substr(0, pos);
}

} // namespace newcore