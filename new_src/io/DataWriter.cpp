#include "io/DataWriter.h"

#include "platform/FileSystem.h"

namespace newcore {

void DataWriter::writeByte(uint8_t v) { buffer_.push_back(v); }

void DataWriter::writeShort(int16_t v) {
	buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
	buffer_.push_back(static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF));
}

void DataWriter::writeUShort(uint16_t v) { writeShort(static_cast<int16_t>(v)); }

void DataWriter::writeInt(int32_t v) {
	uint32_t u = static_cast<uint32_t>(v);
	buffer_.push_back(static_cast<uint8_t>(u & 0xFF));
	buffer_.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
	buffer_.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
	buffer_.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
}

void DataWriter::writeUInt(uint32_t v) { writeInt(static_cast<int32_t>(v)); }

void DataWriter::writeBool(bool b) { writeByte(b ? 1 : 0); }

void DataWriter::writeBytes(const uint8_t* data, size_t count) {
	buffer_.insert(buffer_.end(), data, data + count);
}

bool DataWriter::flushToFile(const std::string& path) const {
	return FileSystem::writeFile(path, buffer_.data(), buffer_.size());
}

} // namespace newcore