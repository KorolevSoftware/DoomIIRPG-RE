#include "io/DataReader.h"

#include <cstring>

namespace newcore {

DataReader::DataReader(const uint8_t* data, size_t size)
	: data_(data), size_(size), pos_(0) {}

DataReader::DataReader(const std::vector<uint8_t>& data)
	: data_(data.data()), size_(data.size()), pos_(0) {}

void DataReader::seek(size_t pos) { pos_ = pos < size_ ? pos : size_; }

void DataReader::skip(size_t count) {
	pos_ += count;
	if (pos_ > size_) pos_ = size_;
}

uint8_t DataReader::readByte() {
	if (pos_ >= size_) return 0;
	return data_[pos_++];
}

uint8_t DataReader::readUByte() { return readByte(); }

int8_t DataReader::readSByte() { return static_cast<int8_t>(readByte()); }

uint16_t DataReader::readUShort() {
	if (pos_ + 2 > size_) { pos_ = size_; return 0; }
	uint16_t v = static_cast<uint16_t>(data_[pos_]) | (static_cast<uint16_t>(data_[pos_ + 1]) << 8);
	pos_ += 2;
	return v;
}

int16_t DataReader::readShort() { return static_cast<int16_t>(readUShort()); }

uint32_t DataReader::readUInt() {
	if (pos_ + 4 > size_) { pos_ = size_; return 0; }
	uint32_t v = static_cast<uint32_t>(data_[pos_])
		| (static_cast<uint32_t>(data_[pos_ + 1]) << 8)
		| (static_cast<uint32_t>(data_[pos_ + 2]) << 16)
		| (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
	pos_ += 4;
	return v;
}

int32_t DataReader::readInt() { return static_cast<int32_t>(readUInt()); }

bool DataReader::readBool() { return readByte() != 0; }

short DataReader::readCoord() {
	return static_cast<short>(readByte() * 8);
}

void DataReader::readBytes(uint8_t* dst, size_t count) {
	size_t available = size_ - pos_;
	if (count > available) count = available;
	if (count > 0) {
		std::memcpy(dst, data_ + pos_, count);
		pos_ += count;
	}
}

std::vector<uint8_t> DataReader::readRemaining() {
	std::vector<uint8_t> out(data_ + pos_, data_ + size_);
	pos_ = size_;
	return out;
}

} // namespace newcore