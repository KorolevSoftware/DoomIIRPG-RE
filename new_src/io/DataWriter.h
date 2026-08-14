#ifndef NEW_IO_DATAWRITER_H
#define NEW_IO_DATAWRITER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace newcore {

// Sequential byte writer backed by a growable buffer. All multi-byte values
// are written little-endian to match the legacy save format.
class DataWriter {
public:
	DataWriter() = default;

	size_t size() const { return buffer_.size(); }
	const std::vector<uint8_t>& buffer() const { return buffer_; }

	void writeByte(uint8_t v);
	void writeUByte(uint8_t v) { writeByte(v); }
	void writeSByte(int8_t v) { writeByte(static_cast<uint8_t>(v)); }
	void writeShort(int16_t v);
	void writeUShort(uint16_t v);
	void writeInt(int32_t v);
	void writeUInt(uint32_t v);
	void writeBool(bool b);
	void writeBytes(const uint8_t* data, size_t count);

	// Writes to a file (little-endian buffer contents). Returns false on failure.
	bool flushToFile(const std::string& path) const;

private:
	std::vector<uint8_t> buffer_;
};

} // namespace newcore

#endif // NEW_IO_DATAWRITER_H