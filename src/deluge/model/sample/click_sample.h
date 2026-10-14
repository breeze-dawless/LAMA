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

#include "definitions_cxx.hpp"
#include <cstdint>
#include <string_view>

/// A short sample - the click / transient layer of the Kick synth mode, which plays it in place of the noise
/// oscillator. It's read from the WAV file completely into RAM as mono 16-bit, so voices play it straight from memory
/// with no streaming or cluster loading involved.
class ClickSample {
public:
	ClickSample() = default;
	~ClickSample() { clear(); }
	ClickSample(const ClickSample&) = delete;
	ClickSample& operator=(const ClickSample&) = delete;

	static constexpr uint32_t kMaxFrames = 65536; ///< ~1.5 s at 44.1 kHz - longer files are cut (and faded) there
	static constexpr int32_t kPositionShift = 12; ///< Playback positions are 20.12 fixed point frames
	static constexpr uint32_t kFinished = 0xFFFFFFFF;

	/// Reads a WAV file: PCM 8 / 16 / 24 / 32 bit or 32-bit float, any number of channels (mixed down to mono), any
	/// sample rate (played back at its own speed). Keeps the current sample if that fails. Must not be called from
	/// the audio routine.
	Error load(std::string_view path);
	/// Copies another one's audio. False if out of memory.
	bool copyFrom(const ClickSample& other);
	void clear();

	[[nodiscard]] bool loaded() const { return data_ != nullptr; }

	/// Writes numSamples values to buffer (overwriting it): the sample from position *pos on at the given amplitude
	/// (scaled like the noise oscillator: full scale comes out at amplitude / 2), then silence once it's over.
	/// Advances *pos, which is 0 at note-on and kFinished once the sample has ended.
	void render(int32_t* buffer, int32_t numSamples, uint32_t* pos, int32_t amplitude) const;
	/// Moves *pos on as if numSamples had been rendered (while the level is 0 the click still runs on in time)
	void skip(int32_t numSamples, uint32_t* pos) const;

private:
	int16_t* data_{nullptr};
	uint32_t length_{0};
	uint32_t step_{1u << kPositionShift}; ///< Per output sample: the file's sample rate against 44.1 kHz
};
