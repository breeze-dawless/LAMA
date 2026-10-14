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

#include "gui/menu_item/menu_item.h"
#include "gui/ui/browser/click_sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/sound/sound.h"
#include <cstring>
#include <string_view>

namespace deluge::gui::menu_item::osc {

/// Kick synth mode: a sample the noise oscillator plays in place of noise - the click layer. Its level is still the
/// noise level. Select opens the file browser, shift + select goes back to plain noise.
class ClickSampleBrowse final : public MenuItem {
public:
	using MenuItem::MenuItem;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return static_cast<Sound*>(modControllable)->synthMode == SynthMode::KICK;
	}

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (Buttons::isShiftButtonPressed()) {
			clear();
			leave();
			return;
		}
		soundEditor.shouldGoUpOneLevelOnBegin = true; // Once the browser closes
		if (!openUI(&clickSampleBrowser)) {
			soundEditor.shouldGoUpOneLevelOnBegin = false;
			leave();
		}
	}

	MenuItem* selectButtonPress() override {
		beginSession(nullptr);
		return NO_NAVIGATION;
	}

	[[nodiscard]] bool allowToBeginSessionFromHorizontalMenu() override { return true; }

	/// In the horizontal menu, shift + select clears without opening anything
	bool onHorizontalItemAction() override {
		if (!Buttons::isShiftButtonPressed()) {
			return false;
		}
		clear();
		return true;
	}

	void getColumnLabel(StringBuf& label) override { label.append("CLICK"); }
	void getNotificationValue(StringBuf& buf) override { appendSampleName(buf); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(name, 32);
		appendSampleName(name);
		hid::display::OLED::main.drawStringCentered(name, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset + 3,
		                                            kTextSpacingX, kTextSpacingY, slot.width);
	}

private:
	static void clear() {
		soundEditor.currentSound->clearClickSample();
		display->displayPopup("NOISE");
	}
	/// Back out of this item when there's no browser to come back from
	static void leave() {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		if (getCurrentUI() == &soundEditor) {
			soundEditor.goUpOneLevel();
		}
	}

	/// The file name without folder and extension, or NOISE
	static void appendSampleName(StringBuf& buf) {
		Sound& sound = *soundEditor.currentSound;
		if (!sound.clickSample.loaded() || sound.clickSamplePath.isEmpty()) {
			buf.append("NOISE");
			return;
		}
		char const* path = sound.clickSamplePath.get();
		char const* slash = strrchr(path, '/');
		char const* name = (slash != nullptr) ? slash + 1 : path;
		char const* dot = strrchr(name, '.');
		buf.append(std::string_view(name, (dot != nullptr) ? static_cast<size_t>(dot - name) : strlen(name)));
	}
};

} // namespace deluge::gui::menu_item::osc
