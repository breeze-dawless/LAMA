/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "model/sample/click_sample.h"
#include "fatfs.hpp"
#include "memory/general_memory_allocator.h"
#include "util/fixedpoint.h"
#include "util/try.h"
#include <algorithm>
#include <bit>
#include <cstring>

namespace {

uint32_t le32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
	       | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) {
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

enum class Encoding : uint8_t { PCM, FLOAT };

/// One sample value of the file's format, as -1..1
float decode(const uint8_t* p, int32_t bytesPerSample, Encoding encoding) {
	if (encoding == Encoding::FLOAT) {
		return std::bit_cast<float>(le32(p));
	}
	switch (bytesPerSample) {
	case 1:
		return (static_cast<float>(p[0]) - 128.0f) * (1.0f / 128.0f); // 8-bit WAV is unsigned
	case 2:
		return static_cast<float>(static_cast<int16_t>(le16(p))) * (1.0f / 32768.0f);
	case 3:
		return static_cast<float>(static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8)
		                                               | (static_cast<uint32_t>(p[1]) << 16)
		                                               | (static_cast<uint32_t>(p[2]) << 24)))
		       * (1.0f / 2147483648.0f);
	default:
		return static_cast<float>(static_cast<int32_t>(le32(p))) * (1.0f / 2147483648.0f);
	}
}

/// Reads exactly buffer.size() bytes at offset
bool readAt(FatFS::File& file, size_t offset, std::span<std::byte> buffer) {
	if (!file.lseek(offset).has_value()) {
		return false;
	}
	auto result = file.read(buffer);
	return result.has_value() && result.value().size() == buffer.size();
}

/// Memory from the general allocator for as long as this exists. The read buffer is only needed while loading, so it
/// shouldn't sit in internal RAM for good.
struct ScopedAllocation {
	explicit ScopedAllocation(uint32_t size) : data(GeneralMemoryAllocator::get().allocLowSpeed(size)) {}
	~ScopedAllocation() {
		if (data != nullptr) {
			delugeDealloc(data);
		}
	}
	ScopedAllocation(const ScopedAllocation&) = delete;
	ScopedAllocation& operator=(const ScopedAllocation&) = delete;
	void* data;
};

constexpr uint32_t kReadBufferSize = 4096;

} // namespace

Error ClickSample::load(std::string_view path) {
	FatFS::File file = D_TRY_CATCH(FatFS::File::open(path, FA_READ), error, {
		return Error::FILE_NOT_FOUND; //
	});
	const size_t fileSize = file.size();

	// RIFF header, then walk the chunks for "fmt " and "data"
	uint8_t header[40];
	if (fileSize < 12 || !readAt(file, 0, {reinterpret_cast<std::byte*>(header), 12})
	    || std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
		return Error::FILE_UNSUPPORTED; // Not a WAV
	}

	uint32_t format = 0;
	uint32_t numChannels = 0;
	uint32_t sampleRate = 0;
	uint32_t bitsPerSample = 0;
	size_t dataStart = 0;
	size_t dataLength = 0;
	bool haveFormat = false;
	bool haveData = false;

	size_t pos = 12;
	while (pos + 8 <= fileSize && !(haveFormat && haveData)) {
		if (!readAt(file, pos, {reinterpret_cast<std::byte*>(header), 8})) {
			return Error::FILE_CORRUPTED;
		}
		const uint32_t chunkSize = le32(header + 4);
		if (std::memcmp(header, "fmt ", 4) == 0) {
			const size_t formatSize = std::min<size_t>(chunkSize, sizeof(header));
			if (formatSize < 16 || !readAt(file, pos + 8, {reinterpret_cast<std::byte*>(header), formatSize})) {
				return Error::FILE_CORRUPTED;
			}
			format = le16(header);
			numChannels = le16(header + 2);
			sampleRate = le32(header + 4);
			bitsPerSample = le16(header + 14);
			if (format == 0xFFFE && formatSize >= 26) { // WAVE_FORMAT_EXTENSIBLE: the sub-format says what it really is
				format = le16(header + 24);
			}
			haveFormat = true;
		}
		else if (std::memcmp(header, "data", 4) == 0) {
			dataStart = pos + 8;
			dataLength = std::min<size_t>(chunkSize, fileSize - dataStart);
			haveData = true;
		}
		pos += 8 + static_cast<size_t>(chunkSize) + (chunkSize & 1);
	}

	const bool pcm =
	    format == 1 && (bitsPerSample == 8 || bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32);
	const bool isFloat = format == 3 && bitsPerSample == 32;
	if (!haveFormat || !haveData || (!pcm && !isFloat) || numChannels == 0 || numChannels > 8 || sampleRate < 4000
	    || sampleRate > 192000) {
		return Error::FILE_UNSUPPORTED;
	}

	const int32_t bytesPerSample = static_cast<int32_t>(bitsPerSample / 8);
	const int32_t frameBytes = bytesPerSample * static_cast<int32_t>(numChannels);
	const uint32_t numFrames = std::min<uint32_t>(dataLength / frameBytes, kMaxFrames);
	if (numFrames < 2) {
		return Error::FILE_CORRUPTED;
	}

	auto* newData = static_cast<int16_t*>(GeneralMemoryAllocator::get().allocLowSpeed(numFrames * sizeof(int16_t)));
	if (newData == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}

	// Read and mix down to mono, a buffer at a time
	ScopedAllocation readAllocation(kReadBufferSize);
	if (readAllocation.data == nullptr) {
		delugeDealloc(newData);
		return Error::INSUFFICIENT_RAM;
	}
	auto* readBuffer = static_cast<uint8_t*>(readAllocation.data);
	const Encoding encoding = isFloat ? Encoding::FLOAT : Encoding::PCM;
	const float channelScale = 32767.0f / static_cast<float>(numChannels);
	const uint32_t framesPerRead = kReadBufferSize / frameBytes;
	for (uint32_t frame = 0; frame < numFrames;) {
		const uint32_t framesNow = std::min(framesPerRead, numFrames - frame);
		if (!readAt(file, dataStart + static_cast<size_t>(frame) * frameBytes,
		            {reinterpret_cast<std::byte*>(readBuffer), static_cast<size_t>(framesNow) * frameBytes})) {
			delugeDealloc(newData);
			return Error::FILE_CORRUPTED;
		}
		const uint8_t* p = readBuffer;
		for (uint32_t f = 0; f < framesNow; f++) {
			float sum = 0.0f;
			for (uint32_t c = 0; c < numChannels; c++) {
				sum += decode(p, bytesPerSample, encoding);
				p += bytesPerSample;
			}
			float value = sum * channelScale;
			if (value != value) {
				value = 0.0f; // NaN in a float file
			}
			newData[frame + f] = static_cast<int16_t>(std::clamp(value, -32768.0f, 32767.0f));
		}
		frame += framesNow;
	}

	// A short fade at the end, so a sample that stops mid-wave (or was cut at kMaxFrames) doesn't end in a step
	const uint32_t fadeFrames = std::min<uint32_t>(64, numFrames);
	for (uint32_t f = 0; f < fadeFrames; f++) {
		int16_t& value = newData[numFrames - 1 - f];
		value = static_cast<int16_t>(static_cast<int32_t>(value) * static_cast<int32_t>(f)
		                             / static_cast<int32_t>(fadeFrames));
	}

	// Swap in. The audio routine can't run in between (no card access here), and voices read through this object
	// every buffer and check positions against the length, so nothing still points at the old data afterwards.
	int16_t* oldData = data_;
	data_ = newData;
	length_ = numFrames;
	step_ = static_cast<uint32_t>((static_cast<uint64_t>(sampleRate) << kPositionShift) / kSampleRate);
	if (oldData != nullptr) {
		delugeDealloc(oldData);
	}
	return Error::NONE;
}

bool ClickSample::copyFrom(const ClickSample& other) {
	if (other.data_ == nullptr) {
		clear();
		return true;
	}
	auto* newData = static_cast<int16_t*>(GeneralMemoryAllocator::get().allocLowSpeed(other.length_ * sizeof(int16_t)));
	if (newData == nullptr) {
		return false;
	}
	std::memcpy(newData, other.data_, other.length_ * sizeof(int16_t));
	clear();
	data_ = newData;
	length_ = other.length_;
	step_ = other.step_;
	return true;
}

void ClickSample::clear() {
	int16_t* oldData = data_;
	data_ = nullptr;
	length_ = 0;
	if (oldData != nullptr) {
		delugeDealloc(oldData);
	}
}

void ClickSample::render(int32_t* buffer, int32_t numSamples, uint32_t* pos, int32_t amplitude) const {
	uint32_t position = *pos;
	const uint32_t end = (data_ != nullptr) ? (length_ - 1) << kPositionShift : 0;
	int32_t n = 0;
	for (; n < numSamples && position < end; n++) {
		const uint32_t index = position >> kPositionShift;
		const int32_t fraction = static_cast<int32_t>(position & ((1u << kPositionShift) - 1));
		const int32_t a = data_[index];
		const int32_t b = data_[index + 1];
		const int32_t value = (a + (((b - a) * fraction) >> kPositionShift)) << 16;
		buffer[n] = multiply_32x32_rshift32(value, amplitude);
		position += step_;
	}
	for (; n < numSamples; n++) {
		buffer[n] = 0;
	}
	*pos = (position < end) ? position : kFinished;
}

void ClickSample::skip(int32_t numSamples, uint32_t* pos) const {
	if (*pos == kFinished) {
		return;
	}
	const uint64_t end = (data_ != nullptr) ? static_cast<uint64_t>(length_ - 1) << kPositionShift : 0;
	const uint64_t position = *pos + static_cast<uint64_t>(step_) * static_cast<uint64_t>(numSamples);
	*pos = (position < end) ? static_cast<uint32_t>(position) : kFinished;
}
