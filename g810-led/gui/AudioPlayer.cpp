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

#include "AudioPlayer.h"

#include <algorithm>
#include <chrono>
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

// Hue in turns (0..1), full saturation, given value.
LedKeyboard::Color fromHue(float hue, float value) {
	hue = hue - std::floor(hue);
	const float sector = hue * 6.0f;
	const int index = (int)sector;
	const float f = sector - (float)index;
	const float p = 0.0f;
	const float q = 1.0f - f;
	const float t = f;
	float rgb[3] = {0, 0, 0};
	switch (index % 6) {
		case 0: rgb[0] = 1; rgb[1] = t; rgb[2] = p; break;
		case 1: rgb[0] = q; rgb[1] = 1; rgb[2] = p; break;
		case 2: rgb[0] = p; rgb[1] = 1; rgb[2] = t; break;
		case 3: rgb[0] = p; rgb[1] = q; rgb[2] = 1; break;
		case 4: rgb[0] = t; rgb[1] = p; rgb[2] = 1; break;
		default: rgb[0] = 1; rgb[1] = p; rgb[2] = q; break;
	}
	LedKeyboard::Color color;
	color.red = toByte(rgb[0] * value);
	color.green = toByte(rgb[1] * value);
	color.blue = toByte(rgb[2] * value);
	return color;
}

}  // namespace

AudioPlayer::AudioPlayer() : m_lastBeat(0), m_flash(0) {
}

AudioPlayer::~AudioPlayer() {
	// Before ~FramePlayer, so stopCapture() still reaches ours.
	stop();
}

bool AudioPlayer::captureFailed() const {
	return m_capture.failed();
}

void AudioPlayer::stopCapture() {
	m_capture.stop();
	m_flash = 0;
}

std::string AudioPlayer::lastError() const {
	std::string error = m_capture.lastError();
	if (error.empty() && writeFailed())
		return "the keyboard write failed — check the connection";
	return error;
}

float AudioPlayer::level() const {
	return m_capture.snapshot().level;
}

void AudioPlayer::setSettings(const Settings &settings) {
	m_settings = settings;
	// The blend lives in FramePlayer, so the toggle has to reach it —
	// the mode, colors and sensitivity below are read per frame.
	setOverlay(settings.overlay);
	m_capture.setGain(settings.gain);
	m_capture.setSmoothing(settings.smoothing);
	m_capture.setAutoGain(settings.autoGain);
}

bool AudioPlayer::start(const std::string &source, const Settings &settings,
                        const std::map<LedKeyboard::Key, Position> &positions,
                        const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
                        Writer writer, Preview preview) {
	stop();
	m_settings = settings;
	m_lastBeat = 0;
	m_flash = 0;
	if (!prepare(positions, base, writer, preview, m_settings.intervalMs,
	             m_settings.overlay))
		return false;

	m_capture.setGain(m_settings.gain);
	m_capture.setSmoothing(m_settings.smoothing);
	m_capture.setAutoGain(m_settings.autoGain);
	if (!m_capture.start(source))
		return false;

	launch();
	return true;
}

LedKeyboard::Color AudioPlayer::gradient(float t) const {
	t = clamp01(t);
	LedKeyboard::Color color;
	color.red = (uint8_t)std::round(m_settings.lowColor.red +
		(m_settings.highColor.red - m_settings.lowColor.red) * t);
	color.green = (uint8_t)std::round(m_settings.lowColor.green +
		(m_settings.highColor.green - m_settings.lowColor.green) * t);
	color.blue = (uint8_t)std::round(m_settings.lowColor.blue +
		(m_settings.highColor.blue - m_settings.lowColor.blue) * t);
	return color;
}

void AudioPlayer::renderFrame(LedKeyboard::KeyValueArray &values) {
	const AudioCapture::Frame frame = m_capture.snapshot();
	const int rowCount = maxRow() - minRow() + 1;
	const int colSpan = std::max(1, maxCol() - minCol());

	if (frame.beats != m_lastBeat) {
		m_lastBeat = frame.beats;
		m_flash = 1.0f;
	} else {
		m_flash *= 0.78f;
		if (m_flash < 0.01f)
			m_flash = 0;
	}

	for (std::map<LedKeyboard::Key, Position>::const_iterator it =
	     positions().begin(); it != positions().end(); ++it) {
		const float colNorm =
			(float)(it->second.second - minCol()) / (float)colSpan;
		// 0 at the bottom row, 1 at the top one.
		const float heightNorm = rowCount > 1 ?
			(float)(maxRow() - it->second.first) / (float)(rowCount - 1) : 0.0f;

		// Band under this column, interpolated between the two nearest.
		const float position = colNorm * (float)(AudioCapture::bandCount - 1);
		int index = (int)position;
		if (index < 0)
			index = 0;
		if (index > AudioCapture::bandCount - 1)
			index = AudioCapture::bandCount - 1;
		const int next = index < AudioCapture::bandCount - 1 ? index + 1 : index;
		const float mix = position - (float)index;
		const float band = frame.bands[index] +
			(frame.bands[next] - frame.bands[index]) * mix;

		// Every mode yields a full-brightness tint plus how much of it
		// this key shows; the blend below decides what the other part
		// of the mix is.
		LedKeyboard::Color tint;
		float amount = 0;
		switch (m_settings.mode) {
			case Mode::bars: {
				// Each row owns one slice of the level range; within its
				// slice the key fades in, so short bars still move.
				const float threshold =
					(float)(maxRow() - it->second.first) / (float)rowCount;
				amount = clamp01((band - threshold) * (float)rowCount);
				tint = gradient(heightNorm);
				break;
			}
			case Mode::rainbow: {
				// Fixed hue per column, brightness from its band.
				tint = fromHue(colNorm * 0.83f, 1.0f);
				amount = band * band;
				break;
			}
			case Mode::level: {
				tint = gradient(frame.level);
				amount = frame.level * frame.level;
				break;
			}
			case Mode::beat: {
				// Flash on the beat, keep a faint glow from the loudness
				// in between so the board never goes fully dark mid-track.
				tint = gradient(frame.bass);
				amount = std::max(m_flash, frame.level * 0.25f);
				break;
			}
		}

		submit(it->first, tint, amount, values);
	}
}

namespace {

const char *modeName(AudioPlayer::Mode mode) {
	switch (mode) {
		case AudioPlayer::Mode::rainbow: return "rainbow";
		case AudioPlayer::Mode::level: return "level";
		case AudioPlayer::Mode::beat: return "beat";
		default: return "bars";
	}
}

AudioPlayer::Mode modeFromName(const std::string &name) {
	if (name == "rainbow") return AudioPlayer::Mode::rainbow;
	if (name == "level") return AudioPlayer::Mode::level;
	if (name == "beat") return AudioPlayer::Mode::beat;
	return AudioPlayer::Mode::bars;
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

bool AudioPlayer::saveState(const std::string &path, const State &state) {
	std::ofstream file(path);
	if (!file.is_open())
		return false;
	char line[128];
	std::snprintf(line, sizeof(line), "vendor %04x\n", state.vendorID);
	file << line;
	std::snprintf(line, sizeof(line), "product %04x\n", state.productID);
	file << line;
	file << "serial " << state.serial << "\n";
	file << "source " << state.source << "\n";
	file << "mode " << modeName(state.settings.mode) << "\n";
	std::snprintf(line, sizeof(line), "low %02x%02x%02x\n",
		state.settings.lowColor.red, state.settings.lowColor.green,
		state.settings.lowColor.blue);
	file << line;
	std::snprintf(line, sizeof(line), "high %02x%02x%02x\n",
		state.settings.highColor.red, state.settings.highColor.green,
		state.settings.highColor.blue);
	file << line;
	std::snprintf(line, sizeof(line), "gain %d\n",
		(int)std::round(state.settings.gain * 100.0f));
	file << line;
	std::snprintf(line, sizeof(line), "smoothing %d\n",
		(int)std::round(state.settings.smoothing * 100.0f));
	file << line;
	file << "autogain " << (state.settings.autoGain ? "1" : "0") << "\n";
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

bool AudioPlayer::loadState(const std::string &path, State &state) {
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
		} else if (tag == "source") {
			// Rest of the line: a source name may contain spaces.
			std::getline(in >> std::ws, state.source);
		} else if (tag == "mode") {
			std::string name;
			in >> name;
			state.settings.mode = modeFromName(name);
		} else if (tag == "low" || tag == "high") {
			std::string hex;
			in >> hex;
			if (tag == "low")
				state.settings.lowColor = colorFromHex(hex);
			else
				state.settings.highColor = colorFromHex(hex);
		} else if (tag == "gain") {
			int value = 100;
			in >> std::dec >> value;
			state.settings.gain = value / 100.0f;
		} else if (tag == "smoothing") {
			int value = 45;
			in >> std::dec >> value;
			state.settings.smoothing = value / 100.0f;
		} else if (tag == "autogain") {
			int value = 1;
			in >> std::dec >> value;
			state.settings.autoGain = value != 0;
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

static AudioPlayer *g_daemonAudio = NULL;

static void audioDaemonSignal(int) {
	if (g_daemonAudio)
		g_daemonAudio->requestStop();
}

int runAudioDaemon(const std::string &path) {
	AudioPlayer::State state;
	if (!AudioPlayer::loadState(path, state)) {
		releasePidFile(path);
		return 1;
	}
	LedKeyboard kbd;
	if (!kbd.open(state.vendorID, state.productID, state.serial)) {
		releasePidFile(path);
		return 1;
	}
	AudioPlayer player;
	g_daemonAudio = &player;
	std::signal(SIGTERM, audioDaemonSignal);
	std::signal(SIGINT, audioDaemonSignal);
	std::signal(SIGHUP, audioDaemonSignal);
	if (!player.start(state.source, state.settings, state.positions, state.base,
			[&kbd](const LedKeyboard::KeyValueArray &values) {
				return kbd.setKeys(values) && kbd.commit();
			})) {
		g_daemonAudio = NULL;
		releasePidFile(path);
		return 1;
	}
	player.runUntilStopped();
	g_daemonAudio = NULL;
	releasePidFile(path);
	return 0;
}
