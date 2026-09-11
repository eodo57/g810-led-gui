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

#include "FramePlayer.h"

#include <chrono>

FramePlayer::FramePlayer() :
		m_minRow(0), m_maxRow(0), m_minCol(0), m_maxCol(1), m_intervalMs(40) {
	m_run = false;
	m_hasFrame = false;
	m_failed = false;
	m_writeFailed = false;
	m_overlay = false;
}

FramePlayer::~FramePlayer() {
	// A subclass destructor has to call stop() itself: by the time this
	// runs its stopCapture() override is already gone. This is only the
	// backstop that joins the worker.
	stop();
}

bool FramePlayer::captureFailed() const {
	return false;
}

void FramePlayer::stopCapture() {
}

bool FramePlayer::isRunning() const {
	return m_run.load();
}

void FramePlayer::requestStop() {
	m_run = false;
}

const std::map<LedKeyboard::Key, LedKeyboard::Color> &
FramePlayer::baseColors() const {
	return m_base;
}

void FramePlayer::setOverlay(bool overlay) {
	m_overlay = overlay;
}

bool FramePlayer::overlay() const {
	return m_overlay.load();
}

const std::map<LedKeyboard::Key, FramePlayer::Position> &
FramePlayer::positions() const {
	return m_positions;
}

int FramePlayer::minRow() const { return m_minRow; }
int FramePlayer::maxRow() const { return m_maxRow; }
int FramePlayer::minCol() const { return m_minCol; }
int FramePlayer::maxCol() const { return m_maxCol; }

bool FramePlayer::writeFailed() const {
	return m_writeFailed.load();
}

bool FramePlayer::prepare(const std::map<LedKeyboard::Key, Position> &positions,
                          const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
                          Writer writer, Preview preview, unsigned intervalMs,
                          bool overlay, bool baseIsOnBoard) {
	m_positions = positions;
	m_base = base;
	m_writer = writer;
	m_preview = preview;
	m_intervalMs = intervalMs ? intervalMs : 40;
	m_overlay = overlay;
	m_lastSent.clear();
	m_pending.clear();
	m_failed = false;
	m_writeFailed = false;
	m_hasFrame = false;
	if (!m_writer || m_positions.empty())
		return false;

	std::map<LedKeyboard::Key, Position>::const_iterator it = m_positions.begin();
	m_minRow = m_maxRow = it->second.first;
	m_minCol = m_maxCol = it->second.second;
	for (; it != m_positions.end(); ++it) {
		m_minRow = std::min(m_minRow, it->second.first);
		m_maxRow = std::max(m_maxRow, it->second.first);
		m_minCol = std::min(m_minCol, it->second.second);
		m_maxCol = std::max(m_maxCol, it->second.second);
	}
	if (baseIsOnBoard)
		m_lastSent = m_base;
	return true;
}

void FramePlayer::launch() {
	m_run = true;
	m_worker = std::thread(&FramePlayer::workerLoop, this);
	// No preview means no main loop to drive a timer: the daemon path
	// calls runUntilStopped() instead.
	if (m_preview)
		m_timer = Glib::signal_timeout().connect(
			sigc::mem_fun(*this, &FramePlayer::onTimer), m_intervalMs);
}

void FramePlayer::runUntilStopped() {
	if (!m_writer || m_positions.empty())
		return;
	while (m_run.load() && !m_failed.load() && !captureFailed()) {
		tick();
		std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
	}
	stop();
}

void FramePlayer::stop() {
	if (m_timer.connected())
		m_timer.disconnect();
	stopCapture();
	// The worker evaluates m_run under m_mutex, so clearing it outside
	// the lock can slip into the window before it registers on the
	// condition variable — the notify would be lost and join() would
	// block the GTK thread forever.
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_run = false;
	}
	m_cv.notify_all();
	if (m_worker.joinable())
		m_worker.join();
	m_pending.clear();
	m_lastSent.clear();
	m_base.clear();
	m_hasFrame = false;
	m_failed = false;
	m_writer = Writer();
	m_preview = Preview();
}

bool FramePlayer::onTimer() {
	if (m_failed.load() || captureFailed()) {
		stop();
		return false;
	}
	tick();
	return m_run.load();
}

void FramePlayer::tick() {
	if (!m_writer || m_positions.empty())
		return;
	LedKeyboard::KeyValueArray values;
	renderFrame(values);
	if (values.empty())
		return;
	if (m_preview)
		m_preview(values);
	pushFrame(values);
}

void FramePlayer::submit(LedKeyboard::Key key, const LedKeyboard::Color &tint,
                         float amount, LedKeyboard::KeyValueArray &values) {
	// Overlaid, a quiet or dark frame leaves every key on its own color,
	// so the legends stay readable; otherwise the effect sits on black.
	LedKeyboard::Color under = {0x00, 0x00, 0x00};
	if (m_overlay.load()) {
		std::map<LedKeyboard::Key, LedKeyboard::Color>::const_iterator found =
			m_base.find(key);
		if (found != m_base.end())
			under = found->second;
	}
	const LedKeyboard::Color color = frames::blend(under, tint, amount);

	std::map<LedKeyboard::Key, LedKeyboard::Color>::const_iterator sent =
		m_lastSent.find(key);
	if (sent != m_lastSent.end() && !frames::worthSending(sent->second, color))
		return;
	LedKeyboard::KeyValue keyValue;
	keyValue.key = key;
	keyValue.color = color;
	values.push_back(keyValue);
	m_lastSent[key] = color;
}

void FramePlayer::pushFrame(const LedKeyboard::KeyValueArray &values) {
	if (values.empty())
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (size_t i = 0; i < values.size(); ++i)
			m_pending[values[i].key] = values[i].color;
		m_hasFrame = true;
	}
	m_cv.notify_one();
}

void FramePlayer::workerLoop() {
	while (true) {
		LedKeyboard::KeyValueArray frame;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this]() {
				return !m_run.load() || m_hasFrame.load();
			});
			if (!m_run && !m_hasFrame)
				return;
			if (m_hasFrame) {
				for (std::map<LedKeyboard::Key, LedKeyboard::Color>::const_iterator
				     it = m_pending.begin(); it != m_pending.end(); ++it) {
					LedKeyboard::KeyValue keyValue;
					keyValue.key = it->first;
					keyValue.color = it->second;
					frame.push_back(keyValue);
				}
				m_pending.clear();
				m_hasFrame = false;
			}
		}
		if (frame.empty()) {
			if (!m_run)
				return;
			continue;
		}
		if (m_writer && !m_writer(frame)) {
			m_writeFailed = true;
			m_failed = true;
			return;
		}
	}
}
