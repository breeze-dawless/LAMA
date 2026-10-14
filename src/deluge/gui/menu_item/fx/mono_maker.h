/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include "dsp/mono_maker.hpp"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "util/d_stringbuf.h"
#include <cstdint>

namespace deluge::gui::menu_item::fx {

/// Sound/Synth only - how much to narrow stereo width below the BassMonoFreq crossover towards
/// mono (0 = bypassed).
class BassMono final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->subBassMonoAmount); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->subBassMonoAmount = current_value;
				}
			}
		}
		else {
			soundEditor.currentModControllable->subBassMonoAmount = current_value;
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }
};

/// Sound/Synth only - where BassMono's narrowing crosses over, shown to the user in Hz rather
/// than the raw 0-50 knob value (see dsp/mono_maker.hpp's crossoverHzFor()).
class BassMonoFreq final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->subBassMonoFreq); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->subBassMonoFreq = current_value;
				}
			}
		}
		else {
			soundEditor.currentModControllable->subBassMonoFreq = current_value;
		}
	}
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

protected:
	int32_t getDisplayValue() override { return dsp::hzForCrossoverFreq(this->getValue()); }
	const char* getUnit() override { return "Hz"; }

	void getNotificationValue(StringBuf& value) override { value.appendInt(getDisplayValue()); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(paramValue, 12);
		paramValue.appendInt(getDisplayValue());
		paramValue.append(getUnit());
		OLED::main.drawStringCentered(paramValue, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}
};

} // namespace deluge::gui::menu_item::fx
