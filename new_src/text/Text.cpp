#include "text/Text.h"

#include <algorithm>
#include <cstring>

namespace newcore {

void Text::reserve(int capacity) {
	buffer_.reserve((size_t)capacity);
}

void Text::resize(int length) {
	length_ = length;
	buffer_.resize((size_t)length);
}

char Text::charAt(int i) const {
	if (i < 0 || i >= length_) return '\0';
	return buffer_[(size_t)i];
}

void Text::setCharAt(char c, int i) {
	if (i < 0 || i >= length_) return;
	buffer_[(size_t)i] = c;
}

Text& Text::append(char c) {
	if ((int)buffer_.size() <= length_) buffer_.resize((size_t)length_ + 1);
	buffer_[(size_t)length_] = c;
	++length_;
	if ((int)buffer_.size() <= length_) buffer_.resize((size_t)length_ + 1);
	buffer_[(size_t)length_] = '\0';
	return *this;
}

Text& Text::append(const char* c) {
	for (const char* p = c; *p; ++p) append(*p);
	return *this;
}

Text& Text::append(int i) {
	return insert(i, length_);
}

Text& Text::append(const Text& t, int i, int i2) {
	for (int k = i; k < i + i2; ++k) {
		char c = t.charAt(k);
		if (c == '\0') break;
		append(c);
	}
	return *this;
}

Text& Text::insert(char c, int i) {
	if (i < 0) i = 0;
	if (i > length_) i = length_;
	buffer_.insert((size_t)i, 1, c);
	++length_;
	return *this;
}

Text& Text::insert(int i, int i2) {
	if (i < 0) {
		insert('-', i2);
		++i2;
		i = -i;
	}
	do {
		insert((char)('0' + i % 10), i2);
		i /= 10;
	} while (i != 0);
	return *this;
}

Text& Text::insert(const char* c, int i, int i2, int i3) {
	if (i3 < 0) i3 = 0;
	if (i3 > length_) i3 = length_;
	for (int k = 0; k < i2; ++k) {
		insert(c[i + k], i3 + k);
	}
	return *this;
}

Text& Text::deleteAt(int i, int i2) {
	if (i < 0) i = 0;
	if (i >= length_) return *this;
	if (i + i2 > length_) i2 = length_ - i;
	buffer_.erase((size_t)i, (size_t)i2);
	length_ -= i2;
	return *this;
}

Text& Text::setLength(int i) {
	if (i < 0) i = 0;
	if (i > length_) {
		buffer_.resize((size_t)i, '\0');
	}
	length_ = i;
	buffer_.resize((size_t)i + 1);
	buffer_[(size_t)i] = '\0';
	return *this;
}

int Text::findFirstOf(char c, int i) const {
	for (int k = i; k < length_; ++k) {
		if (buffer_[(size_t)k] == c) return k;
	}
	return -1;
}

int Text::findLastOf(char c, int n) const {
	for (int k = n - 1; k >= 0; --k) {
		if (buffer_[(size_t)k] == c) return k;
	}
	return -1;
}

int Text::findAnyFirstOf(const char* c, int i) const {
	for (int k = i; k < length_; ++k) {
		for (int j = 0; c[j] != '\0'; ++j) {
			if (buffer_[(size_t)k] == c[j]) return k;
		}
	}
	return -1;
}

void Text::substring(Text& t, int i) const {
	for (int j = i; j < length_; ++j) t.append(buffer_[(size_t)j]);
}

void Text::substring(Text& t, int i, int i2) const {
	for (int j = i; j < i + i2; ++j) {
		if (j >= length_) break;
		t.append(buffer_[(size_t)j]);
	}
}

void Text::dehyphenate(int i, int i2) {
	// Re-search each iteration like the legacy loop (src/Text.cpp:718-725);
	// keeping the first hit across deletions eats following characters.
	int first;
	while ((first = findFirstOf('-', i)) != -1 && first < i + i2) {
		deleteAt(first, 1);
		i2 -= first - i + 2;
		i = ++first;
	}
}

void Text::trim(bool leading, bool trailing) {
	if (leading) {
		while (length_ > 0 && buffer_[0] == ' ') deleteAt(0, 1);
	}
	if (trailing) {
		while (length_ > 0 && buffer_[(size_t)length_ - 1] == ' ') deleteAt(length_ - 1, 1);
	}
}

int Text::wrapText(int start, int maxChars, int maxLines, char breakChar) {
	const char wordBreaks[] = "|\n- ";

	int n4 = 0;
	int n5 = 0;
	int n6 = 0;
	int n7 = start;
	char n8 = '\0';
	bool n9 = false;
	int i = start;
	while (true) {
		int n12 = findAnyFirstOf(wordBreaks, i);
		if (n12 == -1) break;
		n5 += n12 - i;
		if (n9 == false && n8 == '-') --n5;
		if (n5 + ((buffer_[(size_t)n12] == '-') ? 1 : 0) > maxChars || n8 == '|' || n8 == '\n') {
			n4 += i - n7;
			if (n9 != false) --i;
			n7 = insertLineBreak(n7, i - 1, breakChar);
			i = n7 + 1;
			++n6;
			n5 = 1;
			n8 = '\0';
			n9 = false;
			if (maxLines > 0 && n6 == maxLines) {
				setLength(n7);
				return n4;
			}
		} else {
			n9 = false;
			n8 = buffer_[(size_t)n12];
			i = n12 + 1;
			++n5;
			if (n8 == '-' && charAt(i) == '-') {
				n9 = true;
				++i;
			}
		}
	}
	int n10 = n5 + (length_ - i);
	if (n9 == false && n8 == '-') --n10;
	if (n10 > maxChars || n8 == '|' || n8 == '\n') {
		n4 += i - n7;
		if (n9 != false) --i;
		n7 = insertLineBreak(n7, i - 1, breakChar);
		i = n7 + 1;
		++n6;
		if (maxLines > 0 && n6 == maxLines) {
			setLength(n7);
			return n4;
		}
	}
	dehyphenate(n7, length_ - n7);
	return length_;
}

int Text::insertLineBreak(int lineStart, int at, char c) {
	if (at < 0) at = 0;
	if (at >= length_) {
		append(c);
		return length_ - 1;
	}
	if (buffer_[(size_t)at] == '-') {
		++at;
		if (at < length_ && buffer_[(size_t)at] == '-') {
			buffer_[(size_t)at] = c;
		} else {
			insert(c, at);
		}
	} else {
		buffer_[(size_t)at] = c;
	}
	int oldlen = length_;
	dehyphenate(lineStart, at - lineStart - 1);
	return (at - (oldlen - length_)) + 1;
}

int Text::getStringWidth(int i, int i2, bool stopAtBreak) const {
	int n2 = 0;
	int n3 = 0;
	if (i2 == -1 || i2 >= length_) i2 = length_;
	if (i >= 0 && i < i2) {
		for (int j = i; j < i2; ++j) {
			char c = charAt(j);
			if (c == '\n' || c == '|') {
				if (!stopAtBreak) break;
				if (n3 > n2) {
					n2 = n3;
					n3 = 0;
				}
			} else if (c == ' ') {
				n3 += 9;
			} else if (c == '^' && j != i2 - 1) {
				char d = charAt(j + 1);
				if (d >= '0' && d <= '9') {
					++j;
				} else {
					n3 += 9;
				}
			} else {
				n3 += 9;
			}
		}
	}
	if (n3 > n2) n2 = n3;
	return n2;
}

int Text::getNumLines() const {
	int numLines = 1;
	for (int i = 0; i < length_; ++i) {
		char c = charAt(i);
		if (c == '\n' || c == '|') ++numLines;
	}
	return numLines;
}

bool Text::compareTo(const Text& t) const {
	return t.length_ == length_ && std::memcmp(buffer_.c_str(), t.buffer_.c_str(), (size_t)length_) == 0;
}

bool Text::compareTo(const char* str) const {
	size_t len = std::strlen(str);
	if (len != (size_t)length_) return false;
	return std::memcmp(buffer_.c_str(), str, len) == 0;
}

void Text::toLower() {
	for (int i = 0; i < length_; ++i) {
		char c = buffer_[(size_t)i];
		if (c >= 'A' && c <= 'Z') buffer_[(size_t)i] = (char)(c - 'A' + 'a');
	}
}

void Text::toUpper() {
	for (int i = 0; i < length_; ++i) {
		char c = buffer_[(size_t)i];
		if (c >= 'a' && c <= 'z') buffer_[(size_t)i] = (char)(c - 'a' + 'A');
	}
}

} // namespace newcore