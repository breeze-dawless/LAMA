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
#include <bit>
#include <cmath>
#include <cstdint>

/// Building blocks of the Kick synth mode (SynthMode::KICK): its two pitch envelopes and the per-sample oscillator
/// shapes. In Kick mode envelopes 3 and 4 are pitch envelopes - a curved fall from "amount" semitones above the note
/// down to the note over "time" - added together and applied to every oscillator and modulator per sample, so even a
/// sweep of several octaves within a few milliseconds stays smooth (the regular envelopes and pitch modulation only
/// move once per audio block, which turns fast sweeps into audible steps).
namespace deluge::dsp::kick_mode {

constexpr float kSampleRate = 44100.0f;
constexpr int32_t kNumPitchEnvelopes = 2;
constexpr int32_t kAmountMax = 96;  ///< Semitones
constexpr int32_t kTimeSteps = 100; ///< Resolution of the log-scaled time control
constexpr int32_t kCurveMax = 50;   ///< Curves run -50..+50, 0 = straight

// The pitch envelopes' amounts live in envelope 3 / 4's sustain params (linear params, so they're modulatable -
// velocity -> amount is the classic). Their menus show 0..kAmountMax semitones over the param's whole preset range.

/// Linear params: 0..steps -> -full..+full preset
inline int32_t bipolarPresetFromSteps(int32_t steps, int32_t maxSteps) {
	int64_t preset = (static_cast<int64_t>(steps) << 32) / maxSteps - (int64_t{1} << 31);
	return static_cast<int32_t>(std::clamp<int64_t>(preset, INT32_MIN, INT32_MAX));
}
inline int32_t stepsFromBipolarPreset(int32_t preset, int32_t maxSteps) {
	int64_t steps = ((static_cast<int64_t>(preset) + (int64_t{1} << 31)) * maxSteps + (int64_t{1} << 31)) >> 32;
	return static_cast<int32_t>(std::clamp<int64_t>(steps, 0, maxSteps));
}
/// Linear final value -> 0..1 for the menu range (patch cables scale it by 0..2)
inline float normaliseLinear(int32_t finalValue, int32_t neutralValue) {
	return static_cast<float>(finalValue) / (2.0f * static_cast<float>(neutralValue));
}

/// Pitch envelope time: 0.5 ms .. 2 s, log scaled
inline float pitchTimeMs(int32_t steps) {
	return 0.5f * std::pow(4000.0f, static_cast<float>(std::clamp<int32_t>(steps, 0, kTimeSteps)) / kTimeSteps);
}

/// 2^x with a relative error below 2e-5 (0.03 cents when used for pitch)
[[gnu::always_inline]] inline float fastExp2(float x) {
	x = std::clamp(x, -126.0f, 126.0f);
	auto whole = static_cast<int32_t>(x);
	if (static_cast<float>(whole) > x) {
		whole--; // floor, for negative x
	}
	const float f = x - static_cast<float>(whole);
	const float p =
	    1.0f
	    + f
	          * (0.69314718f
	             + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
	return std::bit_cast<float>(std::bit_cast<int32_t>(p) + whole * (1 << 23));
}

/// Falling segment shape: 1 at x = 0, exactly 0 at x = 1. Amount -50..+50: 0 = straight, positive = exponential (a
/// fast drop and a long tail, like an analog kick), negative = slow start and a fast end.
struct Curve {
	float slope{0.0f};
	float floor{0.0f};
	float scale{1.0f};
	bool linear{true};

	void set(int32_t amount) {
		const float k = static_cast<float>(amount) * (8.0f / kCurveMax);
		linear = std::abs(k) < 1e-3f;
		if (linear) {
			return;
		}
		slope = -k * 1.44269504f; // -k / ln(2)
		floor = fastExp2(slope);
		scale = 1.0f / (1.0f - floor);
	}

	[[nodiscard]] float operator()(float x) const { return linear ? 1.0f - x : (fastExp2(slope * x) - floor) * scale; }
};

/// One pitch envelope, ready to evaluate
struct PitchEnvelope {
	float semitones{0.0f};
	float lengthSamples{1.0f};
	float inverseLength{1.0f};
	Curve curve;

	void set(float amountSemitones, int32_t timeSteps, int32_t curveAmount) {
		semitones = amountSemitones;
		lengthSamples = std::max(pitchTimeMs(timeSteps) * (kSampleRate / 1000.0f), 1.0f);
		inverseLength = 1.0f / lengthSamples;
		curve.set(curveAmount);
	}

	/// 0..1 envelope level at a sample position since note-on
	[[nodiscard]] float level(float samplesSinceNoteOn) const {
		return (samplesSinceNoteOn >= lengthSamples) ? 0.0f : curve(samplesSinceNoteOn * inverseLength);
	}
};

/// Waveforms rendered per sample in Kick mode (so the pitch envelopes and FM can move them every sample)
enum class Wave : uint8_t { SINE, TRIANGLE, SQUARE, SAW };

/// Band-limited step correction for a jump of -2 at phase 0 (polyBLEP)
[[gnu::always_inline]] inline float polyBlep(float t, float dt) {
	if (t < dt) {
		const float x = t / dt;
		return x + x - x * x - 1.0f;
	}
	if (t > 1.0f - dt) {
		const float x = (t - 1.0f) / dt;
		return x * x + x + x + 1.0f;
	}
	return 0.0f;
}

/// Sine from a 32-bit phase: a 9th-order odd polynomial on the folded quarter wave (error < 1e-6)
[[gnu::always_inline]] inline float sineOfPhase(uint32_t phase) {
	// Fold to -0.25..0.25 cycles around the nearest zero crossing, keeping the sign
	const float t = static_cast<float>(static_cast<int32_t>(phase)) * (1.0f / 4294967296.0f); // -0.5..0.5
	float x = t;
	if (x > 0.25f) {
		x = 0.5f - x;
	}
	else if (x < -0.25f) {
		x = -0.5f - x;
	}
	const float r = x * 6.28318530718f;
	const float r2 = r * r;
	return r * (1.0f + r2 * (-1.0f / 6.0f + r2 * (1.0f / 120.0f + r2 * (-1.0f / 5040.0f + r2 * (1.0f / 362880.0f)))));
}

/// One sample of a waveform at a phase. dt = phase increment as a fraction of the cycle (for the band-limiting),
/// pulseWidth = the square's high part, 0.5 = square. The phases line up with the regular oscillators (see
/// getOscInitialPhaseForZero()), so a retriggered kick starts at a zero crossing just the same.
[[gnu::always_inline]] inline float waveAt(Wave wave, uint32_t phase, float dt, float pulseWidth) {
	switch (wave) {
	case Wave::SINE:
		return sineOfPhase(phase);
	case Wave::TRIANGLE: {
		// Low point at phase 0, rising through zero at a quarter cycle
		const float t = static_cast<float>(phase) * (1.0f / 4294967296.0f);
		return (t < 0.5f) ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
	}
	case Wave::SAW: {
		// Rising through zero at phase 0, the drop half a cycle later
		const float t = static_cast<float>(phase + 0x80000000u) * (1.0f / 4294967296.0f);
		return t + t - 1.0f - polyBlep(t, dt);
	}
	case Wave::SQUARE: {
		const float t = static_cast<float>(phase) * (1.0f / 4294967296.0f);
		float value = (t < pulseWidth) ? 1.0f : -1.0f;
		value += polyBlep(t, dt);  // Rising edge at 0
		float t2 = t - pulseWidth; // Falling edge at the pulse width
		t2 += (t2 < 0.0f) ? 1.0f : 0.0f;
		value -= polyBlep(t2, dt);
		return value - (pulseWidth + pulseWidth - 1.0f); // No DC offset at any pulse width (kicks are all low end)
	}
	}
	return 0.0f;
}

} // namespace deluge::dsp::kick_mode
