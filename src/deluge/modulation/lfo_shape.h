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

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace deluge::modulation {

/// A user-designed LFO cycle (LFOType::CUSTOM), Serum style: breakpoints joined by segments that can be bent into
/// curves. The points are set and shaped in LFOShapeEditor.
class LFOShape {
public:
	static constexpr int32_t kMaxPoints = 64;
	static constexpr int32_t kMaxCurve = 100;
	static constexpr int32_t kMaxValue = 32767;

	struct Point {
		uint16_t x;   ///< Position in the cycle, 0..65535 (the cycle wraps back to the first point)
		int16_t y;    ///< Value, -32767..32767
		int8_t curve; ///< Bend of the segment from this point to the next: <0 shoots off fast, >0 starts slowly
	};

	enum class Preset : uint8_t { TRIANGLE, SINE, SAW_UP, SAW_DOWN, SQUARE, NUM_PRESETS };

	LFOShape() { loadPreset(Preset::TRIANGLE); }

	[[nodiscard]] int32_t numPoints() const { return numPoints_; }
	[[nodiscard]] const Point& point(int32_t index) const { return points_[index]; }

	/// The value at an LFO phase, as q31 (bipolar, like the other LFO shapes)
	[[nodiscard]] int32_t valueAt(uint32_t phase) const {
		const int32_t x = static_cast<int32_t>(phase >> 16);
		const int32_t i = segmentAt(static_cast<uint16_t>(x));
		const Point& p0 = points_[i];
		const bool last = (i == numPoints_ - 1);
		const int32_t x1 = last ? 65536 : points_[i + 1].x;
		const int32_t y1 = last ? points_[0].y : points_[i + 1].y;
		const int32_t width = std::max<int32_t>(x1 - p0.x, 1);
		const float t = static_cast<float>(x - p0.x) / static_cast<float>(width);
		const float bent = bend(t, p0.curve);
		const float y = static_cast<float>(p0.y) + static_cast<float>(y1 - p0.y) * bent;
		return static_cast<int32_t>(y * 65536.0f);
	}

	/// Index of the segment (= its starting point) containing x
	[[nodiscard]] int32_t segmentAt(uint16_t x) const {
		int32_t i = numPoints_ - 1;
		while (i > 0 && points_[i].x > x) {
			i--;
		}
		return i;
	}

	/// Index of the point at exactly x, or -1
	[[nodiscard]] int32_t pointAt(uint16_t x) const {
		for (int32_t i = 0; i < numPoints_; i++) {
			if (points_[i].x == x) {
				return i;
			}
		}
		return -1;
	}

	/// Sets the value at x: moves the point that's there, or inserts a new one (if there's room). Returns its index,
	/// or -1 if the shape is full.
	int32_t setPoint(uint16_t x, int16_t y) {
		y = clampValue(y);
		const int32_t existing = pointAt(x);
		if (existing >= 0) {
			points_[existing].y = y;
			return existing;
		}
		if (numPoints_ >= kMaxPoints) {
			return -1;
		}
		const int32_t at = segmentAt(x) + 1; // Insert after the segment's start point; the new point inherits its bend
		std::memmove(&points_[at + 1], &points_[at], (numPoints_ - at) * sizeof(Point));
		points_[at] = {x, y, points_[at - 1].curve};
		numPoints_++;
		return at;
	}

	/// Removes a point (the first one, at x = 0, always stays: it anchors the cycle)
	void removePoint(int32_t index) {
		if (index <= 0 || index >= numPoints_) {
			return;
		}
		std::memmove(&points_[index], &points_[index + 1], (numPoints_ - index - 1) * sizeof(Point));
		numPoints_--;
	}

	void setValue(int32_t index, int16_t y) { points_[index].y = clampValue(y); }

	void setCurve(int32_t index, int32_t curve) {
		points_[index].curve = static_cast<int8_t>(std::clamp<int32_t>(curve, -kMaxCurve, kMaxCurve));
	}

	/// Moves a point in time, between its neighbours (the first point stays at 0). Returns the new position.
	uint16_t movePoint(int32_t index, int32_t newX) {
		if (index <= 0 || index >= numPoints_) {
			return 0;
		}
		const int32_t low = points_[index - 1].x + 1;
		const int32_t high = (index == numPoints_ - 1) ? 65535 : points_[index + 1].x - 1;
		points_[index].x = static_cast<uint16_t>(std::clamp<int32_t>(newX, low, high));
		return points_[index].x;
	}

	void loadPreset(Preset preset) {
		constexpr int16_t kTop = kMaxValue;
		constexpr int16_t kBottom = -kMaxValue;
		switch (preset) {
		case Preset::TRIANGLE:
			set({{0, kBottom, 0}, {32768, kTop, 0}});
			break;
		case Preset::SINE:
			// Four bent quarters make a close sine (within 1.5%)
			set({{0, 0, -27}, {16384, kTop, 27}, {32768, 0, -27}, {49152, kBottom, 27}});
			break;
		case Preset::SAW_UP:
			set({{0, kBottom, 0}, {65535, kTop, 0}});
			break;
		case Preset::SAW_DOWN:
			set({{0, kTop, 0}, {65535, kBottom, 0}});
			break;
		case Preset::SQUARE:
			set({{0, kTop, 0}, {32767, kTop, 0}, {32768, kBottom, 0}, {65535, kBottom, 0}});
			break;
		default:
			break;
		}
	}

	/// Compact text form for song / preset files: 10 hex digits per point (x, y, curve). The file reader cuts values
	/// off at 255 characters, so a shape is saved in chunks of kHexChunkPoints points: toHex() writes the chunk
	/// starting at firstPoint (an empty string once there are no more), fromHex() reads the first chunk and
	/// appendHex() each further one.
	static constexpr int32_t kHexChunkPoints = 25;
	static constexpr int32_t kHexBufferSize = kHexChunkPoints * 10 + 1;
	void toHex(char* buffer, int32_t firstPoint) const {
		static constexpr char kDigits[] = "0123456789ABCDEF";
		char* out = buffer;
		const int32_t end = std::min(firstPoint + kHexChunkPoints, numPoints_);
		for (int32_t i = firstPoint; i < end; i++) {
			const uint32_t y = static_cast<uint16_t>(points_[i].y);
			const uint32_t c = static_cast<uint8_t>(points_[i].curve);
			const uint32_t fields[3] = {points_[i].x, y, c};
			const int32_t digits[3] = {4, 4, 2};
			for (int32_t f = 0; f < 3; f++) {
				for (int32_t d = digits[f] - 1; d >= 0; d--) {
					*out++ = kDigits[(fields[f] >> (d * 4)) & 0xF];
				}
			}
		}
		*out = 0;
	}

	/// Reads the first chunk of toHex()'s format. Returns false (shape unchanged) if it isn't valid.
	bool fromHex(const char* text) { return readHex(text, false); }
	/// Reads a further chunk, adding its points after the ones there. Returns false (shape unchanged) if it isn't
	/// valid or doesn't fit.
	bool appendHex(const char* text) { return readHex(text, true); }

private:
	bool readHex(const char* text, bool append) {
		Point read[kMaxPoints];
		int32_t count = append ? numPoints_ : 0;
		if (append) {
			std::memcpy(read, points_, numPoints_ * sizeof(Point));
		}
		const int32_t length = static_cast<int32_t>(std::strlen(text));
		if (length < 10 || length % 10 != 0 || count + length / 10 > kMaxPoints) {
			return false;
		}
		for (int32_t i = 0; i < length / 10; i++) {
			uint32_t values[3] = {0, 0, 0};
			const int32_t digits[3] = {4, 4, 2};
			const char* in = text + i * 10;
			for (int32_t f = 0; f < 3; f++) {
				for (int32_t d = 0; d < digits[f]; d++) {
					const int32_t nibble = hexValue(*in++);
					if (nibble < 0) {
						return false;
					}
					values[f] = (values[f] << 4) | static_cast<uint32_t>(nibble);
				}
			}
			read[count++] = {
			    static_cast<uint16_t>(values[0]), clampValue(static_cast<int16_t>(values[1])),
			    static_cast<int8_t>(std::clamp<int32_t>(static_cast<int8_t>(values[2]), -kMaxCurve, kMaxCurve))};
		}
		// Points must start at 0 and rise strictly
		if (read[0].x != 0) {
			return false;
		}
		for (int32_t i = 1; i < count; i++) {
			if (read[i].x <= read[i - 1].x) {
				return false;
			}
		}
		std::memcpy(points_, read, count * sizeof(Point));
		numPoints_ = count;
		return true;
	}

public:
	bool operator==(const LFOShape& other) const {
		if (numPoints_ != other.numPoints_) {
			return false;
		}
		for (int32_t i = 0; i < numPoints_; i++) {
			const Point& a = points_[i];
			const Point& b = other.points_[i];
			if (a.x != b.x || a.y != b.y || a.curve != b.curve) {
				return false;
			}
		}
		return true;
	}

private:
	int32_t numPoints_{0};
	Point points_[kMaxPoints]{};

	template <int32_t N>
	void set(const Point (&preset)[N]) {
		std::memcpy(points_, preset, sizeof(preset));
		numPoints_ = N;
	}

	static int16_t clampValue(int32_t y) { return static_cast<int16_t>(std::clamp<int32_t>(y, -kMaxValue, kMaxValue)); }

	static int32_t hexValue(char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		}
		if (c >= 'A' && c <= 'F') {
			return c - 'A' + 10;
		}
		if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		}
		return -1;
	}

	/// Serum-style bend: a power curve with an exponent of up to 8 at |curve| = 100. Positive curves start slowly and
	/// end steep, negative ones are the mirror image (shoot off steeply, then settle).
	static float bend(float t, int8_t curve) {
		if (curve == 0 || t <= 0.0f || t >= 1.0f) {
			return std::clamp(t, 0.0f, 1.0f);
		}
		const float exponent = std::exp2(static_cast<float>(std::abs(curve)) * (3.0f / kMaxCurve));
		return (curve > 0) ? std::pow(t, exponent) : 1.0f - std::pow(1.0f - t, exponent);
	}
};

} // namespace deluge::modulation
