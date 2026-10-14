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
#include "dsp/kick_mode.hpp"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/source/patched_param.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

/// Envelopes 3 and 4 in the Kick synth mode are pitch envelopes (see dsp/kick_mode.hpp): these are their controls.
/// The envelope menu shows them, and draws their curve, instead of attack / decay / sustain / release.
namespace deluge::gui::menu_item::envelope::kick_pitch {

namespace kick_mode = deluge::dsp::kick_mode;

/// Envelopes 3 and 4 of a Kick-mode sound are its pitch envelopes
inline bool isPitchEnvelope(ModControllableAudio* modControllable, int32_t envelopeIndex) {
	const auto* sound = static_cast<const Sound*>(modControllable);
	return envelopeIndex >= kNumEnvelopes - kick_mode::kNumPitchEnvelopes && sound->synthMode == SynthMode::KICK;
}

inline int32_t pitchEnvelopeSlot(int32_t envelopeIndex) {
	return envelopeIndex - (kNumEnvelopes - kick_mode::kNumPitchEnvelopes);
}

/// "4.2ms", "60ms", "1.20s"
inline void appendMs(StringBuf& buf, float ms) {
	if (ms < 10.0f) {
		buf.appendFloat(ms, 1, 1);
		buf.append("ms");
	}
	else if (ms < 1000.0f) {
		buf.appendInt(static_cast<int32_t>(ms + 0.5f));
		buf.append("ms");
	}
	else {
		buf.appendFloat(ms / 1000.0f, 2, 2);
		buf.append("s");
	}
}

inline void drawSlotText(const SlotPosition& slot, StringBuf& text) {
	hid::display::OLED::main.drawStringCentered(text, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset + 3,
	                                            kTextSpacingX, kTextSpacingY, slot.width);
}

inline void drawBigText(StringBuf& text) {
	hid::display::OLED::main.drawStringCentred(text.c_str(), 18 + OLED_MAIN_TOPMOST_PIXEL, kTextBigSpacingX,
	                                           kTextBigSizeY);
}

/// The pitch envelope's sweep, drawn in the envelope menu in place of the attack / decay / sustain / release graph.
/// The time axis is stretched (the first milliseconds get more room, so a 3 ms click and a 300 ms tail both show), a
/// dotted line marks the end of the sweep, and the selected control's name and value go top right.
inline void drawPitchEnvelope(int32_t envelopeIndex, const MenuItem* currentItem) {
	const Sound& sound = *soundEditor.currentSound;
	const int32_t k = pitchEnvelopeSlot(envelopeIndex);
	const int32_t preset = soundEditor.currentParamManager->getPatchedParamSet()->getValue(
	    modulation::params::LOCAL_ENV_0_SUSTAIN + envelopeIndex);
	const float semitones = static_cast<float>(kick_mode::stepsFromBipolarPreset(preset, kick_mode::kAmountMax));
	const float timeMs = kick_mode::pitchTimeMs(sound.kickPitchTime[k]);
	kick_mode::Curve curve;
	curve.set(sound.kickPitchCurve[k]);

	hid::display::oled_canvas::Canvas& image = hid::display::OLED::main;
	constexpr int32_t left = 1;
	constexpr int32_t right = OLED_MAIN_WIDTH_PIXELS - 2;
	constexpr int32_t top = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + 4;
	constexpr int32_t bottom = OLED_MAIN_HEIGHT_PIXELS - 3;
	constexpr float kLinearMs = 3.0f;

	const float spanMs = std::max(timeMs * 1.15f, 1.0f);
	const float maxSemitones = std::max(semitones, 12.0f);
	const float logSpan = std::log1p(spanMs / kLinearMs);
	auto msAt = [&](int32_t x) {
		return kLinearMs * std::expm1(logSpan * static_cast<float>(x - left) / static_cast<float>(right - left));
	};
	auto xAt = [&](float ms) {
		return left
		       + static_cast<int32_t>(
		           std::log1p(std::max(ms, 0.0f) / kLinearMs) / logSpan * static_cast<float>(right - left) + 0.5f);
	};
	auto yAt = [&](float ms) {
		const float value = (ms < timeMs) ? semitones * curve(ms / timeMs) : 0.0f;
		const float n = std::clamp(value / maxSemitones, 0.0f, 1.0f);
		return bottom - static_cast<int32_t>(n * static_cast<float>(bottom - top) + 0.5f);
	};

	image.drawHorizontalLine(bottom, left, right);
	int32_t previousY = yAt(0.0f);
	for (int32_t x = left + 1; x <= right; x++) {
		const int32_t y = yAt(msAt(x));
		image.drawLine(x - 1, previousY, x, y);
		previousY = y;
	}

	const int32_t endX = std::clamp(xAt(timeMs), left, right);
	for (int32_t y = top; y < bottom; y += 3) {
		image.drawPixel(endX, y);
	}

	if (currentItem != nullptr) {
		auto* item = const_cast<MenuItem*>(currentItem);
		DEF_STACK_STRING_BUF(readout, 32);
		item->getColumnLabel(readout);
		readout.append(" ");
		item->getNotificationValue(readout);
		const int32_t width = image.getStringWidthInPixels(readout.c_str(), kTextSpacingY);
		image.clearAreaExact(right - width - 1, top - 1, right, top + kTextSpacingY - 1);
		image.drawStringAlignRight(readout.c_str(), top, kTextSpacingX, kTextSpacingY, right + 1);
	}
}

/// Applies a change to the sound being edited - or, with affect-entire held on a kit row, to every kit row
template <typename F>
void forEachEditedSound(F&& apply) {
	if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
		Kit* kit = getCurrentKit();
		for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				apply(*static_cast<SoundDrum*>(drum));
			}
		}
	}
	else {
		apply(*soundEditor.currentSound);
	}
}

/// Amount in semitones: the sweep starts this far above the note. Stored in the envelope's sustain param, so it can
/// be modulated (velocity -> amount) and automated.
class Amount final : public source::PatchedParam, FormattedTitle {
public:
	Amount(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : source::PatchedParam(name, title, modulation::params::LOCAL_ENV_0_SUSTAIN, envelopeIndex),
	      FormattedTitle(title, envelopeIndex + 1), envelopeIndex_(envelopeIndex) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isPitchEnvelope(modControllable, envelopeIndex_);
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kick_mode::kAmountMax; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }

	void getColumnLabel(StringBuf& label) override { label.append("PITCH"); }
	void getNotificationValue(StringBuf& buf) override { appendValueText(buf); }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(text, 16);
		appendValueText(text);
		drawSlotText(slot, text);
	}
	void drawPixelsForOled() override {
		DEF_STACK_STRING_BUF(text, 16);
		appendValueText(text);
		drawBigText(text);
	}

protected:
	void appendValueText(StringBuf& buf) {
		buf.append("+");
		buf.appendInt(getValue());
		buf.append("st");
	}
	void readCurrentValue() override {
		const int32_t preset = soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP());
		setValue(kick_mode::stepsFromBipolarPreset(preset, kick_mode::kAmountMax));
	}
	int32_t getFinalValue() override { return kick_mode::bipolarPresetFromSteps(getValue(), kick_mode::kAmountMax); }

private:
	uint8_t envelopeIndex_;
};

/// A setting kept in the Sound itself (not modulatable): the envelope's time or curve
template <auto field>
class SoundSetting : public IntegerContinuous, FormattedTitle {
public:
	SoundSetting(l10n::String name, l10n::String title, uint8_t envelopeIndex, int32_t minValue, int32_t maxValue)
	    : IntegerContinuous(name, title), FormattedTitle(title, envelopeIndex + 1), envelopeIndex_(envelopeIndex),
	      min_(minValue), max_(maxValue) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return isPitchEnvelope(modControllable, envelopeIndex_);
	}
	bool usesAffectEntire() override { return true; }
	void readCurrentValue() override { setValue((soundEditor.currentSound->*field)[slot()]); }
	void writeCurrentValue() override {
		const int32_t value = getValue();
		forEachEditedSound([&](Sound& sound) {
			(sound.*field)[slot()] = static_cast<std::remove_cvref_t<decltype((sound.*field)[0])>>(value);
		});
	}
	[[nodiscard]] int32_t getMaxValue() const override { return max_; }
	[[nodiscard]] int32_t getMinValue() const override { return min_; }

	void getNotificationValue(StringBuf& buf) override { appendValueText(buf); }
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(text, 16);
		appendValueText(text);
		drawSlotText(slot, text);
	}
	void drawPixelsForOled() override {
		DEF_STACK_STRING_BUF(text, 16);
		appendValueText(text);
		drawBigText(text);
	}

protected:
	virtual void appendValueText(StringBuf& buf) { buf.appendInt(getValue()); }
	[[nodiscard]] int32_t slot() const { return pitchEnvelopeSlot(envelopeIndex_); }

private:
	uint8_t envelopeIndex_;
	int32_t min_;
	int32_t max_;
};

class Time final : public SoundSetting<&Sound::kickPitchTime> {
public:
	Time(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : SoundSetting(name, title, envelopeIndex, 0, kick_mode::kTimeSteps) {}
	void getColumnLabel(StringBuf& label) override { label.append("TIME"); }

protected:
	void appendValueText(StringBuf& buf) override { appendMs(buf, kick_mode::pitchTimeMs(getValue())); }
};

/// -50..+50: 0 = straight, + = exponential (fast drop, long tail), - = slow start, fast end
class Curve final : public SoundSetting<&Sound::kickPitchCurve> {
public:
	Curve(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : SoundSetting(name, title, envelopeIndex, -kick_mode::kCurveMax, kick_mode::kCurveMax) {}
	void getColumnLabel(StringBuf& label) override { label.append("CURVE"); }

protected:
	void appendValueText(StringBuf& buf) override {
		if (getValue() > 0) {
			buf.append("+");
		}
		buf.appendInt(getValue());
	}
};

} // namespace deluge::gui::menu_item::envelope::kick_pitch
