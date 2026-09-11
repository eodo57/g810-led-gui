#include "WavePlayer.h"

#include <algorithm>
#include <cmath>

WavePlayer::WavePlayer() {
}

WavePlayer::~WavePlayer() {
	// Before ~FramePlayer, while this object is still a WavePlayer.
	stop();
}

void WavePlayer::setWave(const Wave &wave) {
	m_wave = wave;
}

void WavePlayer::start(const Wave &wave,
                       const std::map<LedKeyboard::Key, Position> &positions,
                       Writer writer, Preview preview) {
	stop();
	m_wave = wave;
	// The wave owns every key outright, so there is nothing to blend
	// over and no base to preserve.
	if (!prepare(positions, std::map<LedKeyboard::Key, LedKeyboard::Color>(),
	             writer, preview, 80, false))
		return;
	m_origin = std::chrono::steady_clock::now();
	launch();
}

void WavePlayer::renderFrame(LedKeyboard::KeyValueArray &values) {
	const double elapsed = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - m_origin).count();
	float phase = (float)std::fmod(elapsed / m_wave.periodMs, 1.0);
	if (phase < 0)
		phase += 1.0f;

	if (m_wave.mode == Wave::Mode::pulse) {
		// The whole board on one point of the wave.
		const LedKeyboard::Color color = m_wave.sample(phase);
		for (std::map<LedKeyboard::Key, Position>::const_iterator it =
		     positions().begin(); it != positions().end(); ++it)
			submit(it->first, color, 1.0f, values);
		return;
	}
	// Travelling: the phase shifts with the column, so the wave moves
	// across the board.
	const int span = std::max(1, maxCol());
	for (std::map<LedKeyboard::Key, Position>::const_iterator it =
	     positions().begin(); it != positions().end(); ++it) {
		const float spatial = (float)it->second.second / (float)span;
		submit(it->first, m_wave.sample(phase - spatial), 1.0f, values);
	}
}
