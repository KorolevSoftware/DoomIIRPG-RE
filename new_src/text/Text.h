#ifndef NEW_TEXT_TEXT_H
#define NEW_TEXT_TEXT_H

#include <cstdint>
#include <string>

namespace newcore {

// Mutable text buffer mirroring the legacy Text class. The game builds strings
// by appending chars/numbers and then measures/wraps them for UI rendering.
class Text {
public:
	Text() = default;
	explicit Text(int capacity) { reserve(capacity); }
	Text(const Text& other) = default;

	// Capacity management.
	void reserve(int capacity);
	void resize(int length);

	int length() const { return length_; }
	bool empty() const { return length_ == 0; }
	char charAt(int i) const;
	void setCharAt(char c, int i);
	const char* c_str() const { return buffer_.c_str(); }

	Text& append(char c);
	Text& append(uint8_t c) { return append((char)c); }
	Text& append(const char* c);
	Text& append(const std::string& s) { return append(s.c_str()); }
	Text& append(int i);
	Text& append(const Text& t) { return append(t.buffer_); }
	Text& append(const Text& t, int i, int i2);

	Text& insert(char c, int i);
	Text& insert(int i, int i2);
	Text& insert(const char* c, int i, int i2, int i3);

	Text& deleteAt(int i, int i2);
	Text& setLength(int i);

	int findFirstOf(char c) const { return findFirstOf(c, 0); }
	int findFirstOf(char c, int i) const;
	int findLastOf(char c) const { return findLastOf(c, length_); }
	int findLastOf(char c, int n) const;
	int findAnyFirstOf(const char* c, int i) const;

	void substring(Text& t, int i) const;
	void substring(Text& t, int i, int i2) const;

	void dehyphenate() { dehyphenate(0, length_); }
	void dehyphenate(int i, int i2);
	void trim() { trim(true, true); }
	void trim(bool leading, bool trailing);

	// Inserts line-break chars so no line exceeds maxChars. Returns previous
	// length (before wrapping).
	int wrapText(int maxChars) { return wrapText(maxChars, '|'); }
	int wrapText(int maxChars, char breakChar) { return wrapText(0, maxChars, -1, breakChar); }
	int wrapText(int maxChars, int maxLines, char breakChar) { return wrapText(0, maxChars, maxLines, breakChar); }
	int wrapText(int start, int maxChars, int maxLines, char breakChar);
	int insertLineBreak(int lineStart, int at, char c);

	// Width in pixels of the widest line between i and i2. Every glyph is 9px
	// wide; color codes (^N) take no width. If stopAtBreak is true, stops at
	// the first line break.
	int getStringWidth() const { return getStringWidth(0, length_, true); }
	int getStringWidth(bool stopAtBreak) const { return getStringWidth(0, length_, stopAtBreak); }
	int getStringWidth(int i, int i2, bool stopAtBreak) const;

	int getNumLines() const;
	bool compareTo(const Text& t) const;
	bool compareTo(const char* str) const;

	void toLower();
	void toUpper();

private:
	int length_ = 0;
	std::string buffer_;
};

} // namespace newcore

#endif // NEW_TEXT_TEXT_H