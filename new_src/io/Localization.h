#ifndef NEW_IO_LOCALIZATION_H
#define NEW_IO_LOCALIZATION_H

#include <cstdint>
#include <string>
#include <vector>

namespace newcore {

// Text type identifiers used by the game.
enum TextType {
	kTextMain = 0,
	kTextIngame = 1,
	kTextHelp = 2,
	kTextIngame2 = 3,
	kTextMap = 4,
	kTextItem = 5,
	kTextCombat = 6,
	kTextMenu = 7,
	kTextMenu2 = 8,
	kTextStory = 9,
	kTextHud = 10,
	kTextDiary = 11,
	kTextMonster = 12,
	kTextNpc = 13,
	kTextCredits = 14,
	kTextTypeCount = 15,
};

// Static count of strings per text type (from the original game).
struct TextTypeInfo {
	int count;
};

// Localized strings store. Strings are indexed by (type, index) via a text map
// of NUL-separated entries. Format is compatible with the legacy save format.
class Localization {
public:
	static constexpr int kMaxTextTypes = 15;
	static constexpr int kStringIdShift = 10;
	static constexpr int kStringIdMask = 0x3FF;

	bool loadIndex(const std::vector<uint8_t>& idxData);
	void unloadAll();

	// Loads a raw string chunk (strings00/01/02.bin).
	bool loadChunk(int chunk, const std::vector<uint8_t>& data);

	// Loads a text type's strings for the given language (0 = English).
	bool loadTextType(int language, int type);

	// Returns a string (type, index). Empty if not loaded.
	std::string get(int type, int index) const;

	// Text id helpers (id = type << 10 | index).
	int makeId(int type, int index) const { return (type << kStringIdShift) | index; }
	int typeOf(int id) const { return id >> kStringIdShift; }
	int indexOf(int id) const { return id & kStringIdMask; }

	// The name field (before '|') of a string, if present.
	static std::string titleOf(const std::string& s);

	int language() const { return language_; }
	void setLanguage(int language);

	bool isLoaded(int type) const;

private:
	struct TextEntry {
		int chunk;
		int offset;
		int length;
	};

	struct LoadedType {
		std::vector<std::string> strings;
		std::vector<uint16_t> textMap;
	};

	// textIndex: 3 entries per (type, language) pair: chunk, offset, length.
	std::vector<TextEntry> textIndex_;
	int language_ = 0;
	bool loaded_[kMaxTextTypes] = { false };
	LoadedType types_[kMaxTextTypes];

	// Cached raw chunks (strings00/01/02.bin).
	bool chunksLoaded_ = false;
	std::vector<uint8_t> chunks_[3];
};

} // namespace newcore

#endif // NEW_IO_LOCALIZATION_H