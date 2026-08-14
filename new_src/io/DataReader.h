#ifndef NEW_IO_DATAREADER_H
#define NEW_IO_DATAREADER_H

#include <cstdint>
#include <cstddef>
#include <vector>

namespace newcore {

// Sequential byte reader over an in-memory buffer. All multi-byte values are
// read little-endian to match the legacy save format.
class DataReader {
public:
	DataReader(const uint8_t* data, size_t size);
	explicit DataReader(const std::vector<uint8_t>& data);

	const uint8_t* data() const { return data_; }
	size_t size() const { return size_; }
	size_t position() const { return pos_; }
	size_t remaining() const { return size_ - pos_; }
	bool eof() const { return pos_ >= size_; }

	void seek(size_t pos);
	void skip(size_t count);

	uint8_t readByte();
	uint8_t readUByte();
	int8_t readSByte();
	uint16_t readUShort();
	int16_t readShort();
	uint32_t readUInt();
	int32_t readInt();
	bool readBool();

	// Reads a byte and multiplies by 8 (game "coordinate" encoding).
	short readCoord();

	// Raw byte copy.
	void readBytes(uint8_t* dst, size_t count);
	std::vector<uint8_t> readRemaining();

private:
	const uint8_t* data_;
	size_t size_;
	size_t pos_;
};

} // namespace newcore

#endif // NEW_IO_DATAREADER_H