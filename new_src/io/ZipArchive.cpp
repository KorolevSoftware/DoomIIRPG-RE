#include "io/ZipArchive.h"

#include "platform/FileSystem.h"

#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace newcore {

namespace {

constexpr uint32_t kLocalFileSig = 0x04034b50;
constexpr uint32_t kCentralDirSig = 0x02014b50;
constexpr uint32_t kEndOfCentralDirSig = 0x06054b50;
constexpr uint16_t kEncryptedFlag = 0x1;

uint16_t readLE16(const uint8_t* p) { return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8); }
uint32_t readLE32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
		| (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

std::string ZipArchive::normalize(const std::string& name) {
	std::string out;
	out.reserve(name.size());
	for (char c : name) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

bool ZipArchive::open(const std::string& path) {
	entries_.clear();
	fileData_.clear();

	if (!FileSystem::readFile(path, fileData_)) return false;
	if (fileData_.size() < 22) return false;

	const uint8_t* data = fileData_.data();
	size_t size = fileData_.size();

	// Locate the end of central directory record (scan backwards).
	size_t eocd = size;
	{
		size_t scanStart = size > 65557 ? size - 65557 : 0;
		for (size_t i = size - 22 + 1; i > scanStart; --i) {
			if (readLE32(data + i - 1) == kEndOfCentralDirSig) { eocd = i - 1; break; }
		}
	}
	if (eocd == size) return false;

	int count = readLE16(data + eocd + 10);
	uint32_t cdOffset = readLE32(data + eocd + 16);
	uint32_t cdSize = readLE32(data + eocd + 12);

	if (cdOffset + cdSize > size) return false;

	size_t pos = cdOffset;
	for (int i = 0; i < count; ++i) {
		if (pos + 46 > size) return false;
		if (readLE32(data + pos) != kCentralDirSig) return false;

		uint16_t method = readLE16(data + pos + 10);
		uint32_t csize = readLE32(data + pos + 20);
		uint32_t usize = readLE32(data + pos + 24);
		uint16_t nameLen = readLE16(data + pos + 28);
		uint16_t extraLen = readLE16(data + pos + 30);
		uint16_t commentLen = readLE16(data + pos + 32);
		uint32_t offset = readLE32(data + pos + 42);

		std::string name(reinterpret_cast<const char*>(data + pos + 46), nameLen);
		pos += 46 + nameLen + extraLen + commentLen;

		if (method != 0 && method != 8) continue; // stored or deflate only

		Entry e;
		e.offset = offset;
		e.compressedSize = csize;
		e.uncompressedSize = usize;
		e.method = method;
		entries_[normalize(name)] = e;
	}

	return !entries_.empty();
}

void ZipArchive::close() {
	entries_.clear();
	fileData_.clear();
}

bool ZipArchive::hasEntry(const std::string& name) const {
	return entries_.find(normalize(name)) != entries_.end();
}

bool ZipArchive::readEntry(const std::string& name, std::vector<uint8_t>& out) const {
	auto it = entries_.find(normalize(name));
	if (it == entries_.end()) return false;

	const Entry& e = it->second;
	const uint8_t* data = fileData_.data();
	size_t size = fileData_.size();

	size_t pos = e.offset;
	if (pos + 30 > size) return false;
	if (readLE32(data + pos) != kLocalFileSig) return false;

	uint16_t general = readLE16(data + pos + 6);
	if (general & kEncryptedFlag) return false;

	uint16_t nameLen = readLE16(data + pos + 26);
	uint16_t extraLen = readLE16(data + pos + 28);
	size_t contentPos = pos + 30 + nameLen + extraLen;
	if (contentPos + e.compressedSize > size) return false;

	const uint8_t* src = data + contentPos;

	if (e.method == 0) {
		out.assign(src, src + e.uncompressedSize);
		return true;
	}

	// Deflate (method 8). The zip contains raw deflate streams (no zlib header).
	out.resize(e.uncompressedSize);
	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	stream.next_in = const_cast<Bytef*>(src);
	stream.avail_in = e.compressedSize;
	stream.next_out = out.data();
	stream.avail_out = e.uncompressedSize;

	int ret = inflateInit2(&stream, -15);
	if (ret != Z_OK) { out.clear(); return false; }
	ret = inflate(&stream, Z_FINISH);
	int end = inflateEnd(&stream);
	if (ret != Z_STREAM_END || end != Z_OK) { out.clear(); return false; }
	return true;
}

} // namespace newcore