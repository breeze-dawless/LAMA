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

// The Kick synth mode's oscillators (SynthMode::KICK, see dsp/kick_mode.hpp). Sine / triangle / square / saw
// oscillators run per sample here, so the two pitch envelopes (envelopes 3 and 4) and the FM modulators move them
// every sample. Any other oscillator type (sample, wavetable, phi...) renders through the regular subtractive path
// and follows the pitch envelopes once per audio block.

#include "dsp/kick_mode.hpp"
#include "model/voice/voice.h"
#include "modulation/params/param.h"
#include "processing/sound/sound.h"
#include "util/functions.h"
#include <limits>

namespace params = deluge::modulation::params;
namespace kick_mode = deluge::dsp::kick_mode;

namespace {

/// The waveform a basic oscillator type renders as in Kick mode, or false for types rendered the regular way
bool kickWave(OscType type, kick_mode::Wave* wave) {
	switch (type) {
	case OscType::SINE:
		*wave = kick_mode::Wave::SINE;
		return true;
	case OscType::TRIANGLE:
		*wave = kick_mode::Wave::TRIANGLE;
		return true;
	case OscType::SQUARE:
	case OscType::ANALOG_SQUARE:
		*wave = kick_mode::Wave::SQUARE;
		return true;
	case OscType::SAW:
	case OscType::ANALOG_SAW_2:
		*wave = kick_mode::Wave::SAW;
		return true;
	default:
		return false;
	}
}

/// Volume-type param final value -> 0..1 over the menu range
float normaliseVolume(int32_t finalValue, int32_t neutralValue) {
	return std::sqrt(static_cast<float>(std::max(finalValue, int32_t{0})) / (4.0f * static_cast<float>(neutralValue)));
}

constexpr float kToQ31 = 2147483520.0f;
constexpr float kPhaseToUnit = 1.0f / 4294967296.0f;
/// Phase increments are capped just under half a cycle per sample (Nyquist) - a 96 semitone sweep on a high note can
/// ask for more
constexpr float kMaxIncrement = 2.1e9f;

/// A phase offset in cycles (any size - deep FM and feedback add up to a few cycles) as a 32-bit phase
inline uint32_t cyclesToPhase(float cycles) {
	return static_cast<uint32_t>(static_cast<int64_t>(cycles * 4294967296.0f));
}

} // namespace

bool Voice::isKickPerSampleSource(int32_t s) const {
	kick_mode::Wave wave;
	return sound.synthMode == SynthMode::KICK && kickWave(sound.sources[s].oscType, &wave);
}

bool Voice::isKickPitchEnvelope(int32_t e) const {
	return e >= kNumEnvelopes - kick_mode::kNumPitchEnvelopes && sound.synthMode == SynthMode::KICK;
}

/// What a pitch envelope puts out as a modulation source: its 0..1 sweep, encoded like the regular envelopes
int32_t Voice::kickPitchEnvelopeOutput(int32_t e, float samplesSinceNoteOn) const {
	const int32_t k = e - (kNumEnvelopes - kick_mode::kNumPitchEnvelopes);
	kick_mode::PitchEnvelope envelope;
	envelope.set(0.0f, sound.kickPitchTime[k], sound.kickPitchCurve[k]);
	const float level = envelope.level(samplesSinceNoteOn);
	if (level <= 0.0f) {
		return std::numeric_limits<int32_t>::min();
	}
	return static_cast<int32_t>((std::min(level, 1.0f) * 2.0f - 1.0f) * kToQ31);
}

/// The overall pitch adjust for the oscillators rendered the regular way, which follow the pitch envelopes once per
/// buffer (taken at its middle)
int32_t Voice::kickBlockPitchAdjust(int32_t overallPitchAdjust, int32_t numSamples) {
	kick_mode::PitchEnvelope pitchEnvelopes[kick_mode::kNumPitchEnvelopes];
	setUpKickPitchEnvelopes(pitchEnvelopes);
	const float multiplier =
	    kickPitchMultiplierAt(pitchEnvelopes, static_cast<float>(kickSamples) + 0.5f * static_cast<float>(numSamples));
	return static_cast<int32_t>(std::min(static_cast<float>(overallPitchAdjust) * multiplier, kToQ31));
}

void Voice::setUpKickPitchEnvelopes(kick_mode::PitchEnvelope* envelopes) {
	for (int32_t k = 0; k < kick_mode::kNumPitchEnvelopes; k++) {
		const int32_t envelopeIndex = kNumEnvelopes - kick_mode::kNumPitchEnvelopes + k;
		const int32_t amountParam = params::LOCAL_ENV_0_SUSTAIN + envelopeIndex;
		const float amount =
		    std::max(kick_mode::normaliseLinear(paramFinalValues[amountParam], paramNeutralValues[amountParam]), 0.0f)
		    * kick_mode::kAmountMax;
		envelopes[k].set(amount, sound.kickPitchTime[k], sound.kickPitchCurve[k]);
	}
}

float Voice::kickPitchMultiplierAt(const kick_mode::PitchEnvelope* envelopes, float samplesSinceNoteOn) {
	float semitones = 0.0f;
	for (int32_t k = 0; k < kick_mode::kNumPitchEnvelopes; k++) {
		semitones += envelopes[k].semitones * envelopes[k].level(samplesSinceNoteOn);
	}
	return kick_mode::fastExp2(semitones * (1.0f / 12.0f));
}

// oscBuffer holds interleaved stereo if stereoBuffer, else mono. spreadUnison pans the unison parts across it.
[[gnu::hot]] void Voice::renderKickModeSources(ParamManagerForTimeline* paramManager, int32_t* oscBuffer,
                                               int32_t numSamples, bool stereoBuffer, bool spreadUnison,
                                               const int32_t* sourceAmplitudes,
                                               const int32_t* sourceAmplitudeIncrements, const bool* modulatorsActive,
                                               int32_t overallPitchAdjust) {
	kick_mode::PitchEnvelope pitchEnvelopes[kick_mode::kNumPitchEnvelopes];
	setUpKickPitchEnvelopes(pitchEnvelopes);

	// Which sources render here, and as what (the others went through the subtractive path already). The regular
	// sine renders at twice the peak level of the other basic waves, so this one does too.
	kick_mode::Wave waves[kNumSources];
	bool perSample[kNumSources];
	int32_t gainShift[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		perSample[s] = isKickPerSampleSource(s) && sound.isSourceActiveCurrently(s, paramManager)
		               && kickWave(sound.sources[s].oscType, &waves[s]);
		gainShift[s] = (perSample[s] && waves[s] == kick_mode::Wave::SINE) ? 1 : 0;
	}

	if (!perSample[0] && !perSample[1]) {
		return;
	}

	// Per-buffer settings of the per-sample oscillators
	float pulseWidth[kNumSources];
	for (int32_t s = 0; s < kNumSources; s++) {
		const uint32_t pw =
		    static_cast<uint32_t>(lshiftAndSaturate<1>(paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]));
		pulseWidth[s] = std::clamp(0.5f * (1.0f - static_cast<float>(pw) * kPhaseToUnit), 0.02f, 0.98f);
	}

	// FM: each modulator bends its oscillator's phase (modulator 2 can bend modulator 1 instead). Deviation up to two
	// cycles at full level, feedback up to half a cycle.
	float modulatorDepth[kNumModulators];
	float modulatorDepthStep[kNumModulators];
	float feedback[kNumModulators];
	for (int32_t m = 0; m < kNumModulators; m++) {
		const int32_t levelParam = params::LOCAL_MODULATOR_0_VOLUME + m;
		if (!modulatorsActive[m]) {
			modulatorDepth[m] = 0.0f;
			modulatorDepthStep[m] = 0.0f;
			feedback[m] = 0.0f;
			continue;
		}
		const float from = normaliseVolume(modulatorAmplitudeLastTime[m], paramNeutralValues[levelParam]);
		const float to = normaliseVolume(paramFinalValues[levelParam], paramNeutralValues[levelParam]);
		modulatorDepth[m] = 2.0f * from * from;
		modulatorDepthStep[m] = (2.0f * to * to - modulatorDepth[m]) / static_cast<float>(numSamples);
		const int32_t feedbackParam = params::LOCAL_MODULATOR_0_FEEDBACK + m;
		feedback[m] =
		    0.5f
		    * std::clamp(kick_mode::normaliseLinear(paramFinalValues[feedbackParam], paramNeutralValues[feedbackParam]),
		                 0.0f, 1.0f);
	}
	const bool modulator2ToModulator1 = sound.modulator1ToModulator0;

	// The pitch envelopes' multiplier for each sample of this buffer - the same for every unison part
	float multipliers[SSI_TX_BUFFER_NUM_SAMPLES];
	const int32_t numMultipliers = std::min<int32_t>(numSamples, SSI_TX_BUFFER_NUM_SAMPLES);
	for (int32_t n = 0; n < numMultipliers; n++) {
		multipliers[n] = kickPitchMultiplierAt(pitchEnvelopes, static_cast<float>(kickSamples) + static_cast<float>(n));
	}

	for (int32_t u = 0; u < sound.numUnison; u++) {
		VoiceUnisonPart& part = unisonParts[u];

		// Phase increments of this unison part at the note's pitch (the envelopes multiply them per sample)
		float oscIncrement[kNumSources]{};
		bool render[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			render[s] = perSample[s] && part.sources[s].active;
			if (!render[s]) {
				continue;
			}
			uint32_t increment = part.sources[s].phaseIncrementStoredValue;
			if (!adjustPitch(increment, overallPitchAdjust)
			    || !adjustPitch(increment, paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
				render[s] = false;
				continue;
			}
			oscIncrement[s] = static_cast<float>(increment);
		}
		float modIncrement[kNumModulators]{};
		bool modulate[kNumModulators];
		for (int32_t m = 0; m < kNumModulators; m++) {
			modulate[m] = modulatorsActive[m] && part.modulatorPhaseIncrement[m] != 0xFFFFFFFF;
			if (!modulate[m]) {
				continue;
			}
			uint32_t increment = part.modulatorPhaseIncrement[m];
			if (!adjustPitch(increment, overallPitchAdjust)
			    || !adjustPitch(increment, paramFinalValues[params::LOCAL_MODULATOR_0_PITCH_ADJUST + m])) {
				modulate[m] = false;
				continue;
			}
			modIncrement[m] = static_cast<float>(increment);
		}
		if (!render[0] && !render[1]) {
			continue;
		}

		int32_t amplitudeL = 0;
		int32_t amplitudeR = 0;
		if (stereoBuffer) {
			shouldDoPanning(spreadUnison ? sound.unisonPan[u] : 0, &amplitudeL, &amplitudeR);
		}

		int32_t amplitude[kNumSources] = {sourceAmplitudes[0], sourceAmplitudes[1]};
		float depth[kNumModulators] = {modulatorDepth[0], modulatorDepth[1]};
		float modOut[kNumModulators] = {static_cast<float>(part.modulatorFeedback[0]) / kToQ31,
		                                static_cast<float>(part.modulatorFeedback[1]) / kToQ31};

		for (int32_t n = 0; n < numMultipliers; n++) {
			const float multiplier = multipliers[n];

			// Modulators: sines, at their ratio of the (swept) pitch
			float bend[kNumSources] = {0.0f, 0.0f};
			for (int32_t m = kNumModulators - 1; m >= 0; m--) {
				if (!modulate[m]) {
					continue;
				}
				part.modulatorPhase[m] += static_cast<uint32_t>(std::min(modIncrement[m] * multiplier, kMaxIncrement));
				float phaseOffset = feedback[m] * modOut[m];
				if (m == 0 && modulator2ToModulator1 && modulate[1]) {
					phaseOffset += depth[1] * modOut[1];
				}
				const uint32_t phase = part.modulatorPhase[m] + cyclesToPhase(phaseOffset);
				modOut[m] = kick_mode::sineOfPhase(phase);
				depth[m] += modulatorDepthStep[m];
				if (!(m == 1 && modulator2ToModulator1)) {
					bend[m] += depth[m] * modOut[m];
				}
			}

			int32_t mixed = 0;
			int32_t mixedL = 0;
			int32_t mixedR = 0;
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!render[s]) {
					continue;
				}
				const float increment = std::min(oscIncrement[s] * multiplier, kMaxIncrement);
				part.sources[s].oscPos += static_cast<uint32_t>(increment);
				const uint32_t phase = part.sources[s].oscPos + cyclesToPhase(bend[s]);
				const float value = kick_mode::waveAt(waves[s], phase, increment * kPhaseToUnit, pulseWidth[s]);
				amplitude[s] += sourceAmplitudeIncrements[s];
				const int32_t sample =
				    multiply_32x32_rshift32(static_cast<int32_t>(std::clamp(value, -1.0f, 1.0f) * kToQ31), amplitude[s])
				    << gainShift[s];
				if (stereoBuffer) {
					mixedL += multiply_32x32_rshift32(sample, amplitudeL) << 2;
					mixedR += multiply_32x32_rshift32(sample, amplitudeR) << 2;
				}
				else {
					mixed += sample;
				}
			}
			if (stereoBuffer) {
				oscBuffer[n << 1] += mixedL;
				oscBuffer[(n << 1) + 1] += mixedR;
			}
			else {
				oscBuffer[n] += mixed;
			}
		}

		for (int32_t m = 0; m < kNumModulators; m++) {
			part.modulatorFeedback[m] = static_cast<int32_t>(std::clamp(modOut[m], -1.0f, 1.0f) * kToQ31);
		}
	}
}
