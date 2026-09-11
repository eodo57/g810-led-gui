/*
  This file is part of g810-led.

  g810-led is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, version 3 of the License.

  g810-led is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with g810-led.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "ScreenPlayer.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

using frames::clamp01;
using frames::toByte;

LedKeyboard::Color toColor(const float rgb[3]) {
	LedKeyboard::Color color;
	color.red = toByte(rgb[0]);
	color.green = toByte(rgb[1]);
	color.blue = toByte(rgb[2]);
	return color;
}

float peakOf(const float rgb[3]) {
	return std::max(rgb[0], std::max(rgb[1], rgb[2]));
}

}  // namespace

ScreenPlayer::ScreenPlayer() {
}

ScreenPlayer::~ScreenPlayer() {
	// Before ~FramePlayer, so stopCapture() still reaches ours.
	stop();
}

bool ScreenPlayer::captureFailed() const {
	return m_capture.failed();
}

void ScreenPlayer::stopCapture() {
	m_capture.stop();
	m_smoothed.clear();
}

std::string ScreenPlayer::lastError() const {
	const std::string error = m_capture.lastError();
	if (error.empty() && writeFailed())
		return "the keyboard write failed — check the connection";
	return error;
}

bool ScreenPlayer::waiting() const {
	return isRunning() && !m_capture.hasFrame();
}

std::string ScreenPlayer::sourceName() const {
	return m_capture.sourceName();
}

void ScreenPlayer::setSettings(const Settings &settings) {
	m_settings = settings;
	// The blend lives in FramePlayer, so the toggle has to reach it.
	setOverlay(settings.overlay);
}

bool ScreenPlayer::start(const Settings &settings,
                         const std::map<LedKeyboard::Key, Position> &positions,
                         const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
                         Writer writer, Preview preview) {
	stop();
	m_settings = settings;
	m_smoothed.clear();
	if (!prepare(positions, base, writer, preview, m_settings.intervalMs,
	             m_settings.overlay))
		return false;
	if (!m_capture.start())
		return false;
	launch();
	return true;
}

void ScreenPlayer::enhance(float rgb[3]) const {
	const float boost = clamp01(m_settings.boost);
	if (boost <= 0.0f) {
		for (int i = 0; i < 3; ++i)
			rgb[i] = clamp01(rgb[i]);
		return;
	}
	// Saturate around the luminance, so the hue is kept and only how
	// far the colour sits from grey changes.
	const float luminance = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
	for (int i = 0; i < 3; ++i)
		rgb[i] = clamp01(luminance + (rgb[i] - luminance) * (1.0f + 2.0f * boost));

	// Then lift the whole thing towards full brightness, capped so a
	// nearly black screen stays nearly black instead of hunting for
	// colour in the noise.
	const float peak = peakOf(rgb);
	if (peak > 0.02f) {
		const float gain = std::min(1.0f / peak, 1.0f + 3.0f * boost);
		for (int i = 0; i < 3; ++i)
			rgb[i] = clamp01(rgb[i] * gain);
	}
}

void ScreenPlayer::sampleAt(const ScreenCapture::Frame &frame, float x, float y,
                            float rgb[3]) const {
	x = std::max(0.0f, std::min((float)(ScreenCapture::gridWidth - 1), x));
	y = std::max(0.0f, std::min((float)(ScreenCapture::gridHeight - 1), y));
	const int x0 = (int)x;
	const int y0 = (int)y;
	const int x1 = std::min(x0 + 1, ScreenCapture::gridWidth - 1);
	const int y1 = std::min(y0 + 1, ScreenCapture::gridHeight - 1);
	const float fx = x - (float)x0;
	const float fy = y - (float)y0;
	for (int i = 0; i < 3; ++i) {
		const float top = frame.cells[y0][x0][i] * (1.0f - fx) +
		                  frame.cells[y0][x1][i] * fx;
		const float bottom = frame.cells[y1][x0][i] * (1.0f - fx) +
		                     frame.cells[y1][x1][i] * fx;
		rgb[i] = top * (1.0f - fy) + bottom * fy;
	}
}

void ScreenPlayer::renderFrame(LedKeyboard::KeyValueArray &values) {
	const ScreenCapture::Frame frame = m_capture.snapshot();
	if (!frame.valid)
		return;  // still waiting for the portal, or for the first buffer

	const int rowCount = maxRow() - minRow() + 1;
	const int colSpan = std::max(1, maxCol() - minCol());
	const float smoothing = std::max(0.0f, std::min(0.95f, m_settings.smoothing));

	float dominant[3] = {0, 0, 0};
	if (m_settings.mode == Mode::dominant) {
		// Weighted towards the colourful cells: the flat mean of a
		// desktop is a muddy grey, which says nothing about what is on
		// it. Squaring the saturation lets one bright logo win over a
		// large dull background.
		double sums[3] = {0, 0, 0};
		double weights = 0;
		for (int y = 0; y < ScreenCapture::gridHeight; ++y) {
			for (int x = 0; x < ScreenCapture::gridWidth; ++x) {
				const float *cell = frame.cells[y][x];
				const float high = std::max(cell[0], std::max(cell[1], cell[2]));
				const float low = std::min(cell[0], std::min(cell[1], cell[2]));
				const float saturation = high - low;
				const double weight = saturation * saturation * high + 0.02;
				for (int i = 0; i < 3; ++i)
					sums[i] += cell[i] * weight;
				weights += weight;
			}
		}
		if (weights <= 0)
			weights = 1;
		for (int i = 0; i < 3; ++i)
			dominant[i] = (float)(sums[i] / weights);
		enhance(dominant);
	}

	for (std::map<LedKeyboard::Key, Position>::const_iterator it =
	     positions().begin(); it != positions().end(); ++it) {
		float rgb[3];
		if (m_settings.mode == Mode::dominant) {
			for (int i = 0; i < 3; ++i)
				rgb[i] = dominant[i];
		} else {
			// The board laid over the screen: leftmost key at the left
			// edge, top row at the top. Row numbers grow downwards on
			// both, so they map straight across.
			const float colNorm =
				(float)(it->second.second - minCol()) / (float)colSpan;
			const float rowNorm = rowCount > 1 ?
				(float)(it->second.first - minRow()) / (float)(rowCount - 1) : 0.5f;
			sampleAt(frame, colNorm * (float)(ScreenCapture::gridWidth - 1),
				rowNorm * (float)(ScreenCapture::gridHeight - 1), rgb);
			enhance(rgb);
		}

		// Video flickers frame to frame; without this the board strobes.
		if (smoothing > 0.0f) {
			std::map<LedKeyboard::Key, Rgb>::iterator previous =
				m_smoothed.find(it->first);
			if (previous == m_smoothed.end()) {
				Rgb start;
				for (int i = 0; i < 3; ++i)
					start.v[i] = rgb[i];
				m_smoothed[it->first] = start;
			} else {
				for (int i = 0; i < 3; ++i) {
					previous->second.v[i] = previous->second.v[i] * smoothing +
						rgb[i] * (1.0f - smoothing);
					rgb[i] = previous->second.v[i];
				}
			}
		}

		// Overlaid, the dark parts of the screen leave the key on its
		// own colour and only the bright parts take over; otherwise the
		// board is the screen and nothing else.
		const float amount = overlay() ? clamp01(peakOf(rgb)) : 1.0f;
		submit(it->first, toColor(rgb), amount, values);
	}
}

namespace {

const char *modeName(ScreenPlayer::Mode mode) {
	return mode == ScreenPlayer::Mode::dominant ? "dominant" : "mirror";
}

ScreenPlayer::Mode modeFromName(const std::string &name) {
	return name == "dominant" ? ScreenPlayer::Mode::dominant :
		ScreenPlayer::Mode::mirror;
}

LedKeyboard::Color colorFromHex(const std::string &hex) {
	LedKeyboard::Color color;
	color.red = color.green = color.blue = 0;
	if (hex.size() == 6) {
		color.red = (uint8_t)std::strtoul(hex.substr(0, 2).c_str(), NULL, 16);
		color.green = (uint8_t)std::strtoul(hex.substr(2, 2).c_str(), NULL, 16);
		color.blue = (uint8_t)std::strtoul(hex.substr(4, 2).c_str(), NULL, 16);
	}
	return color;
}

}  // namespace

bool ScreenPlayer::saveState(const std::string &path, const State &state) {
	std::ofstream file(path);
	if (!file.is_open())
		return false;
	char line[128];
	std::snprintf(line, sizeof(line), "vendor %04x\n", state.vendorID);
	file << line;
	std::snprintf(line, sizeof(line), "product %04x\n", state.productID);
	file << line;
	file << "serial " << state.serial << "\n";
	file << "mode " << modeName(state.settings.mode) << "\n";
	std::snprintf(line, sizeof(line), "boost %d\n",
		(int)std::round(state.settings.boost * 100.0f));
	file << line;
	std::snprintf(line, sizeof(line), "smoothing %d\n",
		(int)std::round(state.settings.smoothing * 100.0f));
	file << line;
	file << "overlay " << (state.settings.overlay ? "1" : "0") << "\n";
	std::snprintf(line, sizeof(line), "interval %u\n", state.settings.intervalMs);
	file << line;
	for (std::map<LedKeyboard::Key, Position>::const_iterator it =
	     state.positions.begin(); it != state.positions.end(); ++it) {
		std::snprintf(line, sizeof(line), "pos %04x %d %d\n",
			(unsigned)it->first, it->second.first, it->second.second);
		file << line;
	}
	for (std::map<LedKeyboard::Key, LedKeyboard::Color>::const_iterator it =
	     state.base.begin(); it != state.base.end(); ++it) {
		std::snprintf(line, sizeof(line), "base %04x %02x%02x%02x\n",
			(unsigned)it->first, it->second.red, it->second.green,
			it->second.blue);
		file << line;
	}
	return file.good();
}

bool ScreenPlayer::loadState(const std::string &path, State &state) {
	std::ifstream file(path);
	if (!file.is_open())
		return false;
	state.positions.clear();
	state.base.clear();
	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::istringstream in(line);
		std::string tag;
		in >> tag;
		if (tag == "vendor") {
			unsigned value = 0;
			in >> std::hex >> value;
			state.vendorID = (uint16_t)value;
		} else if (tag == "product") {
			unsigned value = 0;
			in >> std::hex >> value;
			state.productID = (uint16_t)value;
		} else if (tag == "serial") {
			in >> state.serial;
		} else if (tag == "mode") {
			std::string name;
			in >> name;
			state.settings.mode = modeFromName(name);
		} else if (tag == "boost") {
			int value = 55;
			in >> std::dec >> value;
			state.settings.boost = value / 100.0f;
		} else if (tag == "smoothing") {
			int value = 50;
			in >> std::dec >> value;
			state.settings.smoothing = value / 100.0f;
		} else if (tag == "overlay") {
			int value = 0;
			in >> std::dec >> value;
			state.settings.overlay = value != 0;
		} else if (tag == "interval") {
			unsigned value = 40;
			in >> std::dec >> value;
			state.settings.intervalMs = value;
		} else if (tag == "pos") {
			unsigned key = 0;
			int row = 0, col = 0;
			in >> std::hex >> key >> std::dec >> row >> col;
			state.positions[(LedKeyboard::Key)key] = Position(row, col);
		} else if (tag == "base") {
			unsigned key = 0;
			std::string hex;
			in >> std::hex >> key >> hex;
			if (hex.size() == 6)
				state.base[(LedKeyboard::Key)key] = colorFromHex(hex);
		}
	}
	return !state.positions.empty();
}

// The GUI tracks us through a pid file next to the state file. Removing
// it on the way out keeps a dead daemon from being mistaken for a live
// one after the pid is recycled.
static void releasePidFile(const std::string &statePath) {
	size_t dot = statePath.rfind(".state");
	if (dot == std::string::npos)
		return;
	std::string pidPath = statePath.substr(0, dot) + ".pid";
	std::ifstream file(pidPath);
	pid_t pid = 0;
	if (file.is_open())
		file >> pid;
	file.close();
	if (pid == getpid())
		::unlink(pidPath.c_str());
}

static ScreenPlayer *g_daemonScreen = NULL;

static void screenDaemonSignal(int) {
	if (g_daemonScreen)
		g_daemonScreen->requestStop();
}

// Detached, with no window of its own. It asks the portal for the
// screen the user already shared; if that permission has gone away the
// portal refuses, the capture reports the failure, and this exits
// rather than putting a dialog somewhere nobody expects one.
int runScreenDaemon(const std::string &path) {
	ScreenPlayer::State state;
	if (!ScreenPlayer::loadState(path, state)) {
		releasePidFile(path);
		return 1;
	}
	LedKeyboard kbd;
	if (!kbd.open(state.vendorID, state.productID, state.serial)) {
		releasePidFile(path);
		return 1;
	}
	ScreenPlayer player;
	g_daemonScreen = &player;
	std::signal(SIGTERM, screenDaemonSignal);
	std::signal(SIGINT, screenDaemonSignal);
	std::signal(SIGHUP, screenDaemonSignal);
	if (!player.start(state.settings, state.positions, state.base,
			[&kbd](const LedKeyboard::KeyValueArray &values) {
				return kbd.setKeys(values) && kbd.commit();
			})) {
		g_daemonScreen = NULL;
		releasePidFile(path);
		return 1;
	}
	player.runUntilStopped();
	g_daemonScreen = NULL;
	releasePidFile(path);
	return 0;
}
