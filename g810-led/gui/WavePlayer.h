#ifndef WAVE_PLAYER
#define WAVE_PLAYER

#include <chrono>
#include <map>

#include "../src/classes/Keyboard.h"
#include "FramePlayer.h"
#include "Wave.h"

// A wave swept across the board, or pulsed over all of it at once.
//
// Only the shape lives here: the worker thread, the merged frames, the
// colour deadband and the timer are FramePlayer's.
class WavePlayer : public FramePlayer {
	public:
		WavePlayer();
		~WavePlayer();

		void start(const Wave &wave,
		           const std::map<LedKeyboard::Key, Position> &positions,
		           Writer writer, Preview preview = Preview());
		void setWave(const Wave &wave);

	protected:
		void renderFrame(LedKeyboard::KeyValueArray &values) override;

	private:
		Wave m_wave;
		std::chrono::steady_clock::time_point m_origin;
};

#endif
