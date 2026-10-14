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
#include "definitions_cxx.hpp"
#include "dsp/kick_mode.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {
class SynthModeSelection final : public Selection {
public:
	/// A sound that's just become a Kick gets a kick right away: the pitch envelopes sweep a click and a body, and
	/// envelope 1 decays out instead of sustaining
	static void setUpKickDefaults(PatchedParamSet& params) {
		namespace p = deluge::modulation::params;
		namespace km = deluge::dsp::kick_mode;
		params.params[p::LOCAL_ENV_2_SUSTAIN].setCurrentValueBasicForSetup(
		    km::bipolarPresetFromSteps(36, km::kAmountMax));
		params.params[p::LOCAL_ENV_3_SUSTAIN].setCurrentValueBasicForSetup(
		    km::bipolarPresetFromSteps(12, km::kAmountMax));
		params.params[p::LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
		    getParamFromUserValue(p::LOCAL_ENV_0_ATTACK, 0));
		params.params[p::LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
		    getParamFromUserValue(p::LOCAL_ENV_0_DECAY, 28));
		params.params[p::LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    getParamFromUserValue(p::LOCAL_ENV_0_SUSTAIN, 0));
		params.params[p::LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    getParamFromUserValue(p::LOCAL_ENV_0_RELEASE, 28));
	}

	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->synthMode); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<::SynthMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->setSynthMode(current_value, currentSong);
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			const bool becomesKick =
			    current_value == ::SynthMode::KICK && soundEditor.currentSound->synthMode != ::SynthMode::KICK;
			soundEditor.currentSound->setSynthMode(current_value, currentSong);
			if (becomesKick) {
				setUpKickDefaults(*soundEditor.currentParamManager->getPatchedParamSet());
				// Kick range: oscillators still at their default pitch go two octaves down (a kit row's C3 becomes C1,
				// 65 Hz). The voices are gone already (setSynthMode() killed them), so the next note picks this up.
				for (Source& source : soundEditor.currentSound->sources) {
					if (source.transpose == 0 && source.oscType != OscType::SAMPLE) {
						source.transpose = -24;
					}
				}
			}
		}
		view.setKnobIndicatorLevels();
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {
		    l10n::getView(l10n::String::STRING_FOR_SUBTRACTIVE),
		    l10n::getView(l10n::String::STRING_FOR_FM),
		    l10n::getView(l10n::String::STRING_FOR_KICK),
		};
	}
};
} // namespace deluge::gui::menu_item
