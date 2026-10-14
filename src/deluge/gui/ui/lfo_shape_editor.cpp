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

#include "gui/ui/lfo_shape_editor.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "util/functions.h"
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

using deluge::modulation::LFOShape;
using namespace deluge::gui;
using namespace deluge::hid::display;

LFOShapeEditor lfoShapeEditor{};

namespace {

constexpr int32_t kNumPresets = static_cast<int32_t>(LFOShape::Preset::NUM_PRESETS);
constexpr const char* kPresetNames[kNumPresets] = {"Triangle", "Sine", "Saw up", "Saw down", "Square"};

constexpr int32_t kValueStep = LFOShape::kMaxValue / 50;      ///< 2 % of the height per detent...
constexpr int32_t kFineValueStep = LFOShape::kMaxValue / 200; ///< ...0.5 % with shift
constexpr int32_t kBendStep = 4;                              ///< Of LFOShape::kMaxCurve = 100
constexpr int32_t kMoveStepsPerPage = 64;                     ///< X encoder steps across the visible part

/// Pad row (0 = bottom) showing a value: the height split into eight equal bands
int32_t rowForValue(int32_t value) {
	return std::clamp<int32_t>((value + LFOShape::kMaxValue) * kDisplayHeight / (2 * LFOShape::kMaxValue + 1), 0,
	                           kDisplayHeight - 1);
}

/// Value a pad row sets: bottom = -100 %, top = +100 %
int16_t valueForRow(int32_t row) {
	return static_cast<int16_t>(row * 2 * LFOShape::kMaxValue / (kDisplayHeight - 1) - LFOShape::kMaxValue);
}

int32_t percent(int32_t value) {
	return (value * 100 + (value >= 0 ? LFOShape::kMaxValue / 2 : -LFOShape::kMaxValue / 2)) / LFOShape::kMaxValue;
}

bool sameShape(const LFOShape& a, const LFOShape& b) {
	if (a.numPoints() != b.numPoints()) {
		return false;
	}
	for (int32_t i = 0; i < a.numPoints(); i++) {
		const LFOShape::Point& p = a.point(i);
		const LFOShape::Point& q = b.point(i);
		if (p.x != q.x || p.y != q.y || p.curve != q.curve) {
			return false;
		}
	}
	return true;
}

} // namespace

LFOShape* LFOShapeEditor::shape() {
	return (sound_ != nullptr) ? sound_->lfoConfig[lfoId_].shape : nullptr;
}

bool LFOShapeEditor::openFor(Sound* sound, uint8_t lfoId) {
	sound_ = sound;
	lfoId_ = lfoId;
	if (shape() == nullptr) {
		return false;
	}
	return openUI(this);
}

bool LFOShapeEditor::opened() {
	// The sound editor underneath keeps blinking its shortcut pads otherwise, right over the shape. It sets the blink
	// up again when it comes back.
	uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	zoomLevel_ = 0;
	page_ = 0;
	select(0);
	startHistory();
	focusRegained();
	return true;
}

ActionResult LFOShapeEditor::exitUI() {
	closeEditor();
	return ActionResult::DEALT_WITH;
}

void LFOShapeEditor::closeEditor() {
	endGesture(); // The history stays, see startHistory()
	closeUI(this);
}

// ---------------------------------------------------------------------------------------------------------------
// Undo / redo

static_assert(std::is_trivially_copyable_v<deluge::modulation::LFOShape>, "States get moved with memmove");
static_assert(std::is_trivially_destructible_v<deluge::modulation::LFOShape>, "States get freed without destructors");

void LFOShapeEditor::startHistory() {
	endGesture();
	LFOShape* s = shape();

	// Back on the LFO the history belongs to, and nothing else changed its shape meanwhile (a preset load, another
	// song...): carry on where it left off, so leaving the editor doesn't cost the undo steps
	if (history_ != nullptr && historyCount_ > 0 && sound_ == historySound_ && lfoId_ == historyLfo_ && s != nullptr
	    && sameShape(history_[historyCursor_].shape, *s)) {
		select(history_[historyCursor_].selected);
		return;
	}

	if (history_ == nullptr) {
		// Kept from then on: a few KB, and the next LFO's history reuses it
		void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(State) * kHistorySize);
		if (memory != nullptr) {
			history_ = static_cast<State*>(memory);
			for (int32_t i = 0; i < kHistorySize; i++) {
				new (&history_[i]) State{};
			}
		}
	}
	historyCount_ = 0;
	historyCursor_ = 0;
	historySound_ = sound_;
	historyLfo_ = lfoId_;
	if (history_ != nullptr && s != nullptr) {
		history_[0] = {*s, selected_};
		historyCount_ = 1;
	}
	// Without the memory the editor just works without undo
}

void LFOShapeEditor::beginEdit(Edit edit) {
	const uint32_t now = AudioEngine::audioSampleTimer;
	const bool carriesOn = edit != Edit::OTHER && edit == lastEdit_ && selected_ == lastEditPoint_
	                       && now - lastEditTime_ < static_cast<uint32_t>(kSampleRate);
	lastEdit_ = edit;
	lastEditPoint_ = selected_;
	lastEditTime_ = now;
	if (carriesOn || history_ == nullptr || historyCount_ == 0) {
		return;
	}
	// A new step: what could have been redone is gone, and the oldest step goes when the history is full
	if (historyCursor_ == kHistorySize - 1) {
		std::memmove(&history_[0], &history_[1], sizeof(State) * (kHistorySize - 1));
		historyCursor_--;
	}
	historyCursor_++;
	historyCount_ = historyCursor_ + 1;
}

void LFOShapeEditor::commitEdit() {
	if (history_ != nullptr && historyCount_ > 0 && shape() != nullptr) {
		history_[historyCursor_] = {*shape(), selected_};
	}
}

void LFOShapeEditor::restore(const State& state) {
	if (LFOShape* s = shape()) {
		*s = state.shape;
		select(state.selected);
	}
}

void LFOShapeEditor::stepHistory(int32_t steps) {
	endGesture();
	const bool backwards = steps < 0;
	int32_t target = historyCursor_;
	if (history_ != nullptr && historyCount_ > 0) {
		target = std::clamp<int32_t>(historyCursor_ + steps, 0, historyCount_ - 1);
	}
	if (target == historyCursor_) {
		display->displayPopup(backwards ? "NO UNDO" : "NO REDO");
		return;
	}
	historyCursor_ = target;
	restore(history_[historyCursor_]);
	if (display->haveOLED()) {
		// Where in the history that leaves it: "UNDO 3/7" = step 3 of 7
		char buffer[24];
		snprintf(buffer, sizeof(buffer), "%s %d/%d", backwards ? "UNDO" : "REDO", static_cast<int>(historyCursor_),
		         static_cast<int>(historyCount_ - 1));
		display->displayPopup(buffer);
	}
	else {
		display->displayPopup(backwards ? "UNDO" : "REDO");
	}
}

void LFOShapeEditor::focusRegained() {
	changed();
}

void LFOShapeEditor::changed() {
	uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		char buffer[8];
		snprintf(buffer, sizeof(buffer), "P%d", static_cast<int>(selected_ + 1));
		display->setText(buffer);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Selection and view

void LFOShapeEditor::select(int32_t index) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	selected_ = std::clamp<int32_t>(index, 0, s->numPoints() - 1);
	page_ = std::min<int32_t>(static_cast<int32_t>(s->point(selected_).x / pageWidth()), numPages() - 1);
}

void LFOShapeEditor::selectAt(uint32_t x) {
	if (LFOShape* s = shape()) {
		select(s->segmentAt(static_cast<uint16_t>(std::min<uint32_t>(x, 65535))));
	}
}

void LFOShapeEditor::selectStep(int32_t steps) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	endGesture();
	const int32_t n = s->numPoints();
	select(((selected_ + steps) % n + n) % n);
}

void LFOShapeEditor::showPage(int32_t page) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	endGesture();
	page_ = std::clamp<int32_t>(page, 0, numPages() - 1);
	const uint32_t start = pageStart();
	const uint32_t end = start + pageWidth();
	for (int32_t i = 0; i < s->numPoints(); i++) {
		if (s->point(i).x >= start && s->point(i).x < end) {
			selected_ = i;
			break;
		}
	}
}

void LFOShapeEditor::zoom(int32_t direction) {
	endGesture();
	zoomLevel_ = std::clamp<int32_t>(zoomLevel_ + ((direction > 0) ? 1 : -1), 0, kMaxZoomLevel);
	select(selected_); // The page with the selected point
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "Zoom x%d", static_cast<int>(numPages()));
	display->displayPopup(buffer);
}

// ---------------------------------------------------------------------------------------------------------------
// Editing

bool LFOShapeEditor::splitComplete(int32_t level) {
	LFOShape* s = shape();
	const uint32_t step = pageWidth() >> level;
	for (uint32_t x = pageStart(); x < pageStart() + pageWidth(); x += step) {
		if (s->pointAt(static_cast<uint16_t>(x)) < 0) {
			return false;
		}
	}
	return true;
}

// Splits the visible part into 2, 4, 8, 16 equal parts, one level per detent - or takes the finest split back out.
// The new points sit on the shape as it is, so splitting changes nothing until they're moved.
void LFOShapeEditor::split(int32_t direction) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	const uint16_t keepX = s->point(selected_).x;
	char buffer[24];

	if (direction > 0) {
		for (int32_t level = 1; level <= kMaxSplitLevel; level++) {
			if (splitComplete(level)) {
				continue;
			}
			// Read the shape first: every point added bends the segments it splits a little differently
			const uint32_t step = pageWidth() >> level;
			uint16_t newX[1 << kMaxSplitLevel];
			int16_t newY[1 << kMaxSplitLevel];
			int32_t count = 0;
			for (uint32_t x = pageStart(); x < pageStart() + pageWidth(); x += step) {
				if (s->pointAt(static_cast<uint16_t>(x)) < 0) {
					newX[count] = static_cast<uint16_t>(x);
					newY[count] = static_cast<int16_t>(s->valueAt(x << 16) >> 16);
					count++;
				}
			}
			if (s->numPoints() + count > LFOShape::kMaxPoints) {
				display->displayPopup("FULL");
				return;
			}
			beginEdit(Edit::OTHER);
			for (int32_t i = 0; i < count; i++) {
				s->setPoint(newX[i], newY[i]);
			}
			selectAt(keepX);
			commitEdit();
			snprintf(buffer, sizeof(buffer), "%d parts", 1 << level);
			display->displayPopup(buffer);
			return;
		}
		display->displayPopup("MAX");
		return;
	}

	for (int32_t level = kMaxSplitLevel; level >= 1; level--) {
		if (!splitComplete(level)) {
			continue;
		}
		// Every other point of this split goes - the coarser split's points stay
		const uint32_t step = pageWidth() >> level;
		beginEdit(Edit::OTHER);
		for (uint32_t x = pageStart() + step; x < pageStart() + pageWidth(); x += 2 * step) {
			s->removePoint(s->pointAt(static_cast<uint16_t>(x)));
		}
		selectAt(keepX);
		commitEdit();
		if (level > 1) {
			snprintf(buffer, sizeof(buffer), "%d parts", 1 << (level - 1));
			display->displayPopup(buffer);
		}
		else {
			display->displayPopup("NO SPLIT");
		}
		return;
	}
	display->displayPopup("NO SPLIT");
}

void LFOShapeEditor::moveSelected(int32_t offset) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	if (selected_ == 0) {
		display->displayPopup("FIXED"); // The first point anchors the cycle at its start
		return;
	}
	const int32_t steps = Buttons::isShiftButtonPressed() ? kMoveStepsPerPage * 4 : kMoveStepsPerPage;
	const int32_t step = std::max<int32_t>(static_cast<int32_t>(pageWidth()) / steps, 1);
	beginEdit(Edit::MOVE);
	const uint16_t x = s->movePoint(selected_, s->point(selected_).x + offset * step);
	commitEdit();
	page_ = std::min<int32_t>(static_cast<int32_t>(x / pageWidth()), numPages() - 1);
	popupPosition();
}

void LFOShapeEditor::changeValue(int32_t offset) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	const int32_t step = Buttons::isShiftButtonPressed() ? kFineValueStep : kValueStep;
	beginEdit(Edit::VALUE);
	s->setValue(selected_, static_cast<int16_t>(std::clamp<int32_t>(s->point(selected_).y + offset * step,
	                                                                -LFOShape::kMaxValue, LFOShape::kMaxValue)));
	commitEdit();
	popupValue();
}

// Turning right bows the segment upwards, whichever way it runs (the curve's sign depends on that)
void LFOShapeEditor::bend(int32_t offset, bool segmentBefore) {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	const int32_t n = s->numPoints();
	const int32_t segment = segmentBefore ? (selected_ - 1 + n) % n : selected_;
	const int32_t y0 = s->point(segment).y;
	const int32_t y1 = s->point((segment + 1) % n).y;
	const int32_t sign = (y1 > y0) ? -1 : 1;
	beginEdit(segmentBefore ? Edit::BEND_BEFORE : Edit::BEND_AFTER);
	s->setCurve(segment, s->point(segment).curve + sign * offset * kBendStep);
	commitEdit();
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%s %+d", segmentBefore ? "Bend <" : "Bend >",
	         static_cast<int>(s->point(segment).curve * sign));
	display->displayPopup(buffer);
}

void LFOShapeEditor::deleteSelected() {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	if (selected_ == 0) {
		display->displayPopup("FIXED");
		return;
	}
	const int32_t index = selected_;
	beginEdit(Edit::OTHER);
	s->removePoint(index);
	select(index - 1);
	commitEdit();
	display->displayPopup("DELETED");
}

void LFOShapeEditor::loadNextPreset() {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	beginEdit(Edit::OTHER);
	s->loadPreset(static_cast<LFOShape::Preset>(nextPreset_));
	display->displayPopup(kPresetNames[nextPreset_]);
	nextPreset_ = (nextPreset_ + 1) % kNumPresets;
	zoomLevel_ = 0;
	select(0);
	commitEdit();
}

void LFOShapeEditor::popupPoint() {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "Point %d/%d", static_cast<int>(selected_ + 1), static_cast<int>(s->numPoints()));
	display->displayPopup(buffer);
}

void LFOShapeEditor::popupValue() {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%+d%%", static_cast<int>(percent(s->point(selected_).y)));
	display->displayPopup(buffer);
}

void LFOShapeEditor::popupPosition() {
	LFOShape* s = shape();
	if (s == nullptr) {
		return;
	}
	const int32_t permille = static_cast<int32_t>(s->point(selected_).x) * 1000 / 65536;
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%d.%d%%", static_cast<int>(permille / 10), static_cast<int>(permille % 10));
	display->displayPopup(buffer);
}

void LFOShapeEditor::popupPage() {
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "Page %d/%d", static_cast<int>(page_ + 1), static_cast<int>(numPages()));
	display->displayPopup(buffer);
}

// ---------------------------------------------------------------------------------------------------------------
// Controls

ActionResult LFOShapeEditor::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	// Only these buttons belong to the editor. Everything else goes on to the usual handling - shift above all, whose
	// release must get through or shift stays held (also after leaving).
	const bool shiftYPress = (b == Y_ENC && on && Buttons::isShiftButtonPressed());
	if (b != BACK && b != SELECT_ENC && b != SYNTH && b != KIT && b != MIDI && b != CV && !shiftYPress) {
		return ActionResult::NOT_DEALT_WITH;
	}
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (!on) {
		return ActionResult::DEALT_WITH;
	}
	// Back leaves, as in every menu - straight away, so holding it on leaves the sound editor as well (its long-press
	// timer is already running). Undo / redo have the sidebar pads and shift + select.
	if (b == BACK || b == SELECT_ENC) {
		closeEditor();
		return ActionResult::DEALT_WITH;
	}
	if (shiftYPress) {
		loadNextPreset();
	}
	else {
		const int32_t direction = (b == SYNTH || b == KIT) ? -1 : 1;
		if (Buttons::isShiftButtonPressed()) {
			if (numPages() > 1) {
				showPage(page_ + direction);
				popupPage();
			}
			else {
				display->displayPopup("ZOOM IN");
			}
		}
		else {
			selectStep(direction);
			popupPoint();
		}
	}
	changed();
	return ActionResult::DEALT_WITH;
}

ActionResult LFOShapeEditor::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (velocity == 0) {
		return ActionResult::DEALT_WITH;
	}
	LFOShape* s = shape();
	if (s == nullptr) {
		return ActionResult::DEALT_WITH;
	}

	// Sidebar: the first column's pads jump to a page, the second column has undo and redo
	if (x >= kDisplayWidth) {
		if (x == kDisplayWidth && y < numPages()) {
			showPage(y);
			popupPage();
			changed();
		}
		else if (x == kDisplayWidth + 1 && (y == kUndoPadRow || y == kRedoPadRow)) {
			stepHistory((y == kUndoPadRow) ? -1 : 1);
			changed();
		}
		return ActionResult::DEALT_WITH;
	}

	const uint32_t columnStart = pageStart() + static_cast<uint32_t>(x) * columnWidth();
	const uint32_t columnEnd = columnStart + columnWidth();
	auto inColumn = [&](int32_t i) { return s->point(i).x >= columnStart && s->point(i).x < columnEnd; };

	int32_t firstInColumn = -1;
	for (int32_t i = 0; i < s->numPoints(); i++) {
		if (inColumn(i)) {
			firstInColumn = i;
			break;
		}
	}

	if (Buttons::isShiftButtonPressed()) {
		// Removes the points in the column (the first point of the cycle stays)
		if (firstInColumn >= 0 && (firstInColumn > 0 || (s->numPoints() > 1 && inColumn(1)))) {
			beginEdit(Edit::OTHER);
			for (int32_t i = s->numPoints() - 1; i > 0; i--) {
				if (inColumn(i)) {
					s->removePoint(i);
				}
			}
			selectAt(columnStart);
			commitEdit();
		}
	}
	else if (firstInColumn < 0 && s->numPoints() >= LFOShape::kMaxPoints) {
		display->displayPopup("FULL");
	}
	else {
		// A point already in this column gets the new height, otherwise there's a new point
		beginEdit(Edit::OTHER);
		int32_t index = firstInColumn;
		if (index >= 0) {
			s->setValue(index, valueForRow(y));
		}
		else {
			index = s->setPoint(static_cast<uint16_t>(columnStart), valueForRow(y));
		}
		select(index);
		commitEdit();
		popupValue();
	}
	changed();
	return ActionResult::DEALT_WITH;
}

ActionResult LFOShapeEditor::horizontalEncoderAction(int32_t offset) {
	if (Buttons::isButtonPressed(deluge::hid::button::X_ENC)) {
		zoom(offset);
	}
	else {
		moveSelected(offset);
	}
	changed();
	return ActionResult::DEALT_WITH;
}

ActionResult LFOShapeEditor::verticalEncoderAction(int32_t offset, bool inCardRoutine) {
	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}
	if (Buttons::isButtonPressed(deluge::hid::button::Y_ENC)) {
		split(offset);
	}
	else {
		selectStep(offset);
		popupPoint();
	}
	changed();
	return ActionResult::DEALT_WITH;
}

void LFOShapeEditor::selectEncoderAction(int8_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		stepHistory(offset); // Runs through the undo history, a step per detent
	}
	else {
		selectStep(offset);
		popupPoint();
	}
	changed();
}

void LFOShapeEditor::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	if (whichModEncoder == 1) { // Upper gold knob
		changeValue(offset);
	}
	else {
		bend(offset, Buttons::isButtonPressed(deluge::hid::button::MOD_ENCODER_0));
	}
	changed();
}

void LFOShapeEditor::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	// The lower knob's press only changes what turning it does, see modEncoderAction()
	if (whichModEncoder == 1 && on) {
		deleteSelected();
		changed();
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

bool LFOShapeEditor::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) {
	if (image == nullptr) {
		return true;
	}
	LFOShape* s = shape();
	const int32_t zeroRow = rowForValue(0);
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			image[y][x] = colours::black;
		}
		if (s == nullptr) {
			continue;
		}
		// The shape as a bar from the centre line, read in the middle of the column
		const uint32_t columnStart = pageStart() + static_cast<uint32_t>(x) * columnWidth();
		const int32_t row = rowForValue(s->valueAt((columnStart + columnWidth() / 2) << 16) >> 16);
		for (int32_t y = std::min(row, zeroRow); y <= std::max(row, zeroRow); y++) {
			image[y][x] = colours::blue.dim(2);
		}
		image[row][x] = colours::blue;

		// Points in this column, at their own height - the selected one white
		for (int32_t i = 0; i < s->numPoints(); i++) {
			const uint32_t pointX = s->point(i).x;
			if (pointX >= columnStart && pointX < columnStart + columnWidth()) {
				image[rowForValue(s->point(i).y)][x] = (i == selected_) ? colours::white : colours::cyan;
			}
		}
	}
	return true;
}

bool LFOShapeEditor::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (image == nullptr) {
		return true;
	}
	// The pages while zoomed in, bottom up: the visible one bright
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		image[y][kDisplayWidth] = (numPages() > 1 && y < numPages())
		                              ? ((y == page_) ? colours::white : colours::blue.dim(2))
		                              : colours::black;
		image[y][kDisplayWidth + 1] = colours::black;
	}
	// Undo and redo: bright while there's a step to take (dark without the memory for a history)
	if (history_ != nullptr) {
		image[kUndoPadRow][kDisplayWidth + 1] = canUndo() ? colours::red : colours::red.dim(3);
		image[kRedoPadRow][kDisplayWidth + 1] = canRedo() ? colours::green : colours::green.dim(3);
	}
	return true;
}

void LFOShapeEditor::drawShape(oled_canvas::Canvas& canvas, const LFOShape& shape, int32_t minX, int32_t minY,
                               int32_t maxX, int32_t maxY) {
	const int32_t width = maxX - minX;
	const int32_t height = maxY - minY;
	int32_t previousY = -1;
	for (int32_t px = 0; px <= width; px++) {
		const uint32_t phase = static_cast<uint32_t>((static_cast<uint64_t>(px) << 32) / (width + 1));
		const int32_t value = shape.valueAt(phase) >> 16; // -32767..32767
		const int32_t y = maxY - (value + LFOShape::kMaxValue) * height / (2 * LFOShape::kMaxValue);
		if (previousY >= 0) {
			canvas.drawLine(minX + px - 1, previousY, minX + px, y);
		}
		else {
			canvas.drawPixel(minX + px, y);
		}
		previousY = y;
	}
}

void LFOShapeEditor::renderOLED(oled_canvas::Canvas& canvas) {
	LFOShape* s = shape();
	char title[24];
	if (s != nullptr) {
		snprintf(title, sizeof(title), "LFO%d P%d/%d", static_cast<int>(lfoId_ + 1), static_cast<int>(selected_ + 1),
		         static_cast<int>(s->numPoints()));
	}
	else {
		snprintf(title, sizeof(title), "LFO%d", static_cast<int>(lfoId_ + 1));
	}
	canvas.drawScreenTitle(title);

	// The page top right, like the horizontal menus do it: "2/4"
	{
		constexpr int32_t y = 1 + OLED_MAIN_TOPMOST_PIXEL;
		char pageText[8];
		snprintf(pageText, sizeof(pageText), "%d/%d", static_cast<int>(page_ + 1), static_cast<int>(numPages()));
		canvas.drawStringAlignRight(pageText, y, kTextSpacingX, kTextSpacingY);
	}

	if (s == nullptr) {
		return;
	}
	constexpr int32_t kMinX = 2;
	constexpr int32_t kMaxX = OLED_MAIN_WIDTH_PIXELS - 3;
	constexpr int32_t kMinY = kScreenTitleSeparatorY + 3;
	constexpr int32_t kMaxY = OLED_MAIN_HEIGHT_PIXELS - 4;
	constexpr int32_t kWidth = kMaxX - kMinX;
	constexpr int32_t kHeight = kMaxY - kMinY;
	const uint32_t start = pageStart();
	const uint32_t visible = pageWidth();

	auto toScreenY = [&](int32_t value) {
		return kMaxY - (value + LFOShape::kMaxValue) * kHeight / (2 * LFOShape::kMaxValue);
	};

	// Centre line, dotted, and a tick under every pad column
	const int32_t zeroY = toScreenY(0);
	for (int32_t x = kMinX; x <= kMaxX; x += 3) {
		canvas.drawPixel(x, zeroY);
	}
	for (int32_t column = 0; column <= kDisplayWidth; column++) {
		canvas.drawPixel(kMinX + column * kWidth / kDisplayWidth, kMaxY + 2);
	}

	// The visible part of the shape
	int32_t previousY = -1;
	for (int32_t px = 0; px <= kWidth; px++) {
		const uint32_t x = start + static_cast<uint32_t>(static_cast<uint64_t>(px) * visible / (kWidth + 1));
		const int32_t y = toScreenY(s->valueAt(x << 16) >> 16);
		if (previousY >= 0) {
			canvas.drawLine(kMinX + px - 1, previousY, kMinX + px, y);
		}
		previousY = y;
	}

	// Its points: small squares, the selected one bigger and filled
	for (int32_t i = 0; i < s->numPoints(); i++) {
		const uint32_t pointX = s->point(i).x;
		if (pointX < start || pointX >= start + visible) {
			continue;
		}
		const int32_t px = kMinX + static_cast<int32_t>(static_cast<uint64_t>(pointX - start) * kWidth / visible);
		const int32_t py = toScreenY(s->point(i).y);
		if (i == selected_) {
			canvas.invertArea(px - 2, 5, py - 2, py + 2);
			canvas.drawRectangle(px - 3, py - 3, px + 3, py + 3);
		}
		else {
			canvas.drawRectangle(px - 1, py - 1, px + 1, py + 1);
		}
	}
}
