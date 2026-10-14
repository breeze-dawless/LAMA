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

#include "gui/ui/browser/click_sample_browser.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include <cstring>

namespace {
char const* allowedFileExtensionsClick[] = {"WAV", nullptr};
}

ClickSampleBrowser::ClickSampleBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	title = "Click sample";
	shouldWrapFolderContents = false;
}

bool ClickSampleBrowser::opened() {
	if (!Browser::opened()) {
		return false;
	}

	qwertyAlwaysVisible = false;
	allowedFileExtensions = allowedFileExtensionsClick;
	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;
	fileIndexSelected = 0;

	Error error = StorageManager::initSD();
	if (error != Error::NONE) {
sdError:
		display->displayError(error);
		display->setNextTransitionDirection(0); // Cancel the transition that we'll now not be doing
		return false;
	}

	{
		// Start at the current click sample, or in SAMPLES
		char const* searchFilename = nullptr;
		String currentPath;
		currentPath.set(&soundEditor.currentSound->clickSamplePath);
		if (currentPath.isEmpty()) {
			currentDir.set("SAMPLES");
		}
		else {
			char const* currentPathChars = currentPath.get();
			char const* slashAddress = strrchr(currentPathChars, '/');
			if (slashAddress == nullptr) {
				searchFilename = currentPathChars;
				currentDir.clear();
			}
			else {
				const int32_t slashPos = static_cast<int32_t>(slashAddress - currentPathChars);
				searchFilename = &currentPathChars[slashPos + 1];
				currentDir.set(currentPathChars);
				currentDir.shorten(slashPos);
			}
		}

		error = arrivedInNewFolder(1, searchFilename, "SAMPLES");
		if (error != Error::NONE) {
			goto sdError;
		}
	}

	return true;
}

Error ClickSampleBrowser::getCurrentFilePath(String* path) {
	path->set(&currentDir);
	const int32_t oldLength = path->getLength();
	if (oldLength != 0) {
		Error error = path->concatenateAtPos("/", oldLength);
		if (error != Error::NONE) {
			path->clear();
			return error;
		}
	}

	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		path->clear();
		return Error::FILE_NOT_FOUND;
	}
	Error error = path->concatenate(&currentFileItem->filename);
	if (error != Error::NONE) {
		path->clear();
	}
	return error;
}

void ClickSampleBrowser::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (currentFileItem == nullptr) {
		return;
	}

	if (currentFileItem->isFolder) {
		char const* filenameChars = currentFileItem->filename.get();
		Error error = goIntoFolder(filenameChars);
		if (error != Error::NONE) {
			display->displayError(error);
			close();
		}
		return;
	}

	String path;
	Error error = getCurrentFilePath(&path);
	close();
	if (error != Error::NONE || path.isEmpty()) {
		display->displayError(error != Error::NONE ? error : Error::FILE_NOT_FOUND);
		return;
	}

	error = soundEditor.currentSound->setClickSample(path.get());
	if (error != Error::NONE) {
		display->displayError(error);
	}
}

ClickSampleBrowser clickSampleBrowser{};
