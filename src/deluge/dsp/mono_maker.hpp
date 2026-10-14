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

#include "util/fixedpoint.h"
#include <cmath>
#include <cstdint>

namespace deluge::dsp {

/// Persistent 2-pole lowpass state for one channel of the sub-bass mono crossover below. Two of
/// these (L and R) belong on whatever owns the signal (e.g. a Voice, alongside its existing
/// per-channel saturation state) - like that state, this is a running IIR filter that must survive
/// across render() calls for one note/voice, reset only when that note/voice starts fresh.
struct MonoMakerState {
	q31_t s1 = 0;
	q31_t s2 = 0;
};

namespace mono_maker_detail {

/// UI-facing 0-50 range for the crossover frequency knob, and the Hz range it maps to
/// log-linearly (perceptually even steps across the knob, like a real filter cutoff control).
/// 22 (~121Hz) is the default, matching this feature's original fixed ~120Hz crossover before this
/// control existed.
constexpr int32_t kFreqUiMax = 50;
constexpr float kMinCrossoverHz = 40.0f;
constexpr float kMaxCrossoverHz = 500.0f;
constexpr float kLog2MinCrossoverHz = 5.321928f; // log2(40), precomputed to avoid a runtime log2f
constexpr float kLog2MaxCrossoverHz = 8.965784f; // log2(500)

/// UI raw (0-50) -> actual combined -3dB crossover frequency in Hz.
[[gnu::always_inline]] inline float crossoverHzFor(uint8_t freqRaw) {
	float t = static_cast<float>(freqRaw) / static_cast<float>(kFreqUiMax);
	return exp2f(kLog2MinCrossoverHz + t * (kLog2MaxCrossoverHz - kLog2MinCrossoverHz));
}

/// UI-facing 0-50 range for the mono-blend amount knob.
constexpr int32_t kBlendUiMax = 50;

/// multiply_32x32_rshift32() intentionally returns half the true product (see fixedpoint.h) so it
/// can be restored with a saturating double instead of a plain "*2", which can overflow/wrap right
/// at the negative full-scale rail (same lesson applied in dsp/filter/svf.cpp's Club Filter).
[[gnu::always_inline]] inline q31_t doubleSaturate(q31_t halfValue) {
	return add_saturate(halfValue, halfValue);
}

/// blendAmount (0-50) as a q31 fraction.
[[gnu::always_inline]] inline q31_t blendToFraction(uint8_t blendAmount) {
	constexpr q31_t stepQ31 = ONE_Q31 / kBlendUiMax;
	return static_cast<q31_t>(blendAmount) * stepQ31;
}

/// One-pole stage alpha for a combined 2-pole (12dB/octave) crossover once cascaded twice - same
/// construction, and the same 0.6436 cascade-correction factor, as MultibandCompressor's band
/// splitter would use; kept here as its own small self-contained copy rather than a shared utility,
/// matching this codebase's existing precedent of each DSP unit owning its crossover math
/// independently.
[[gnu::always_inline]] inline q31_t cascadedLowpass2Pole(q31_t input, MonoMakerState& state, q31_t alpha) {
	q31_t diff1 = subtract_saturate(input, state.s1);
	state.s1 = add_saturate(state.s1, doubleSaturate(multiply_32x32_rshift32(diff1, alpha)));
	q31_t diff2 = subtract_saturate(state.s1, state.s2);
	state.s2 = add_saturate(state.s2, doubleSaturate(multiply_32x32_rshift32(diff2, alpha)));
	return state.s2;
}

/// Linear interpolation from `a` (amount=0) to `b` (amount=50), computed as two independently
/// scaled-and-saturated terms summed together - NOT as `a + (b-a)*fraction`. That more obvious
/// formula first computes `b-a` via subtract_saturate, which silently clamps and loses information
/// whenever `a` and `b` sit at opposite full-scale rails - exactly the case here, since a fully
/// anti-phase stereo signal's low band and its L/R average can legitimately end up near-opposite.
[[gnu::always_inline]] inline q31_t lerpQ31(q31_t a, q31_t b, uint8_t amount) {
	q31_t fractionB = blendToFraction(amount);
	q31_t fractionA = subtract_saturate(ONE_Q31, fractionB);
	q31_t termA = doubleSaturate(multiply_32x32_rshift32(a, fractionA));
	q31_t termB = doubleSaturate(multiply_32x32_rshift32(b, fractionB));
	return add_saturate(termA, termB);
}

} // namespace mono_maker_detail

/// Converts the 0-50 UI crossover-frequency knob to the alpha constant monoizeSubBass() needs.
/// Involves a single exp2f, so call this ONCE per render buffer (not per sample) and pass the
/// result in.
inline q31_t alphaForCrossoverFreq(uint8_t freqRaw) {
	using namespace mono_maker_detail;
	float stageHz = crossoverHzFor(freqRaw) / 0.6436f;
	float alphaFraction = 2.0f * static_cast<float>(M_PI) * stageHz / 44100.0f;
	return static_cast<q31_t>(alphaFraction * static_cast<float>(ONE_Q31));
}

/// Same 0-50 knob, as the actual combined -3dB crossover frequency in Hz (rounded), for display.
inline int32_t hzForCrossoverFreq(uint8_t freqRaw) {
	return static_cast<int32_t>(mono_maker_detail::crossoverHzFor(freqRaw) + 0.5f);
}

/// Narrows stereo width below the given crossover, leaving everything above it untouched - the
/// standard "mono bass" trick for club/vinyl translation and a tighter low end. Splits each
/// channel into low/high, averages the two low bands into one shared mono low signal, blends each
/// channel's own low band towards that shared average (amount 0-50: 0 leaves the input completely
/// unchanged, 50 makes everything below the crossover identical/fully mono), then recombines with
/// each channel's untouched high band. `alpha` comes from alphaForCrossoverFreq() above.
[[gnu::always_inline]] inline void monoizeSubBass(q31_t inputL, q31_t inputR, MonoMakerState& stateL,
                                                  MonoMakerState& stateR, uint8_t amount, q31_t alpha, q31_t* outL,
                                                  q31_t* outR) {
	if (amount == 0) {
		*outL = inputL;
		*outR = inputR;
		return;
	}
	q31_t lowL = mono_maker_detail::cascadedLowpass2Pole(inputL, stateL, alpha);
	q31_t lowR = mono_maker_detail::cascadedLowpass2Pole(inputR, stateR, alpha);
	q31_t highL = subtract_saturate(inputL, lowL);
	q31_t highR = subtract_saturate(inputR, lowR);

	// (lowL + lowR) / 2, shifting each down first so the sum can never exceed full scale.
	q31_t monoLow = (lowL >> 1) + (lowR >> 1);

	q31_t blendedLowL = mono_maker_detail::lerpQ31(lowL, monoLow, amount);
	q31_t blendedLowR = mono_maker_detail::lerpQ31(lowR, monoLow, amount);

	*outL = add_saturate(blendedLowL, highL);
	*outR = add_saturate(blendedLowR, highR);
}

} // namespace deluge::dsp
