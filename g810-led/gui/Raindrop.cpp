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

#include "Raindrop.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unistd.h>
#include <string>

RaindropAnimation::RaindropAnimation() {
	std::srand((unsigned)std::time(NULL));
}

RaindropAnimation::~RaindropAnimation() {
	// Before ~FramePlayer, while stopCapture() still reaches ours.
	stop();
}

void RaindropAnimation::start(
		const std::map<LedKeyboard::Key, Position> &positions,
		const LedKeyboard::Color &color, Writer writer,
		unsigned intervalMs,
		const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
		Preview preview) {
	stop();
	m_color = color;
	m_amount.clear();
	m_drops.clear();
	m_ripples.clear();
	m_frame = 0;
	// A non-empty base means "blend the drops over these colours", and
	// they are what the board is already showing, so the first frame
	// only has to write the keys a drop has actually reached.
	const bool overlay = !base.empty();
	if (!prepare(positions, base, writer, preview,
	             intervalMs ? intervalMs : 100, overlay, overlay))
		return;
	launch();
}

void RaindropAnimation::stopCapture() {
	m_drops.clear();
	m_ripples.clear();
	m_amount.clear();
	m_frame = 0;
}

void RaindropAnimation::renderFrame(LedKeyboard::KeyValueArray &values) {
	for (std::map<LedKeyboard::Key, float>::iterator it = m_amount.begin();
	     it != m_amount.end(); ++it)
		it->second *= 0.80f;

	// Spawn a drop every few frames, starting above the top row, on the
	// column of a random key so it always falls onto keys.
	if (m_frame % 6 == 0 && m_drops.size() < 5) {
		std::map<LedKeyboard::Key, Position>::const_iterator it =
			positions().begin();
		std::advance(it, std::rand() % positions().size());
		Drop drop;
		drop.col = it->second.second;
		drop.y = -0.5f;
		drop.speed = 0.6f;
		m_drops.push_back(drop);
	}

	// Advance drops: light the head and a fading trail above it.
	for (std::vector<Drop>::iterator it = m_drops.begin();
	     it != m_drops.end();) {
		it->y += it->speed;
		bool alive = true;
		for (int trail = 0; trail < 4; trail++) {
			int row = (int)it->y - trail;
			float amount = 1.0f - (float)trail / 4.0f;
			for (std::map<LedKeyboard::Key, Position>::const_iterator p =
			     positions().begin(); p != positions().end(); ++p) {
				if (p->second.first == row &&
				    std::abs(p->second.second - it->col) <= 2)
					addLight(p->first, amount);
			}
		}
		if ((int)it->y > maxRow()) {
			// Splash: an expanding ripple from the bottom.
			Ripple ripple;
			ripple.row = maxRow();
			ripple.col = it->col;
			ripple.radius = 0.5f;
			m_ripples.push_back(ripple);
			alive = false;
		}
		if (alive)
			++it;
		else
			it = m_drops.erase(it);
	}

	// Advance ripples: light the expanding ring.
	for (std::vector<Ripple>::iterator it = m_ripples.begin();
	     it != m_ripples.end();) {
		it->radius += 0.7f;
		bool alive = it->radius < 12.0f;
		float amount = std::max(0.0f, 1.0f - it->radius / 12.0f);
		for (std::map<LedKeyboard::Key, Position>::const_iterator p =
		     positions().begin(); p != positions().end(); ++p) {
			float dRow = (float)(p->second.first - it->row);
			float dCol = (float)(p->second.second - it->col);
			float distance = std::sqrt(dRow * dRow + dCol * dCol);
			if (std::abs(distance - it->radius) < 1.0f)
				addLight(p->first, amount * 0.8f);
		}
		if (alive)
			++it;
		else
			it = m_ripples.erase(it);
	}

	m_frame++;
	emitLights(values);
}

void RaindropAnimation::addLight(LedKeyboard::Key key, float amount) {
	float &value = m_amount[key];
	value = std::min(1.0f, value + amount);
}

// How lit each key is becomes a colour: the drop colour mixed into
// whatever is underneath it — the base scheme when overlaying, black
// otherwise — which is exactly what submit() does. A key that has faded
// out is written back to what is underneath and then forgotten.
void RaindropAnimation::emitLights(LedKeyboard::KeyValueArray &values) {
	std::vector<LedKeyboard::Key> faded;
	for (std::map<LedKeyboard::Key, float>::iterator it = m_amount.begin();
	     it != m_amount.end(); ++it) {
		const float amount = std::min(1.0f, it->second);
		const bool lit = amount > 0.02f;
		if (!lit)
			faded.push_back(it->first);
		submit(it->first, m_color, lit ? amount : 0.0f, values);
	}
	for (LedKeyboard::Key key : faded)
		m_amount.erase(key);
}

bool RaindropAnimation::saveState(const std::string &path, const State &state) {
	std::ofstream file(path);
	if (!file.is_open())
		return false;
	char line[128];
	std::snprintf(line, sizeof(line), "vendor %04x\n", state.vendorID);
	file << line;
	std::snprintf(line, sizeof(line), "product %04x\n", state.productID);
	file << line;
	file << "serial " << state.serial << "\n";
	std::snprintf(line, sizeof(line), "interval %u\n", state.intervalMs);
	file << line;
	file << "overlay " << (state.overlay ? "1" : "0") << "\n";
	std::snprintf(line, sizeof(line), "color %02x%02x%02x\n",
		state.color.red, state.color.green, state.color.blue);
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

bool RaindropAnimation::loadState(const std::string &path, State &state) {
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
		} else if (tag == "interval") {
			in >> std::dec >> state.intervalMs;
		} else if (tag == "overlay") {
			int value = 0;
			in >> value;
			state.overlay = value != 0;
		} else if (tag == "color") {
			std::string hex;
			in >> hex;
			if (hex.size() == 6) {
				state.color.red = (uint8_t)std::strtoul(hex.substr(0, 2).c_str(), NULL, 16);
				state.color.green = (uint8_t)std::strtoul(hex.substr(2, 2).c_str(), NULL, 16);
				state.color.blue = (uint8_t)std::strtoul(hex.substr(4, 2).c_str(), NULL, 16);
			}
		} else if (tag == "pos") {
			unsigned key = 0;
			int row = 0, col = 0;
			in >> std::hex >> key >> std::dec >> row >> col;
			state.positions[(LedKeyboard::Key)key] = Position(row, col);
		} else if (tag == "base") {
			unsigned key = 0;
			std::string hex;
			in >> std::hex >> key >> hex;
			if (hex.size() == 6) {
				LedKeyboard::Color color;
				color.red = (uint8_t)std::strtoul(hex.substr(0, 2).c_str(), NULL, 16);
				color.green = (uint8_t)std::strtoul(hex.substr(2, 2).c_str(), NULL, 16);
				color.blue = (uint8_t)std::strtoul(hex.substr(4, 2).c_str(), NULL, 16);
				state.base[(LedKeyboard::Key)key] = color;
			}
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

static RaindropAnimation *g_daemonRain = NULL;

static void rainDaemonSignal(int) {
	if (g_daemonRain)
		g_daemonRain->requestStop();
}

int runRainDaemon(const std::string &path) {
	RaindropAnimation::State state;
	if (!RaindropAnimation::loadState(path, state)) {
		releasePidFile(path);
		return 1;
	}
	LedKeyboard kbd;
	if (!kbd.open(state.vendorID, state.productID, state.serial)) {
		releasePidFile(path);
		return 1;
	}
	RaindropAnimation rain;
	g_daemonRain = &rain;
	std::signal(SIGTERM, rainDaemonSignal);
	std::signal(SIGINT, rainDaemonSignal);
	std::signal(SIGHUP, rainDaemonSignal);
	std::map<LedKeyboard::Key, LedKeyboard::Color> base;
	if (state.overlay)
		base = state.base;
	rain.start(state.positions, state.color,
		[&kbd](const LedKeyboard::KeyValueArray &values) {
			return kbd.setKeys(values) && kbd.commit();
		}, state.intervalMs, base);
	rain.runUntilStopped();
	g_daemonRain = NULL;
	releasePidFile(path);
	return 0;
}
