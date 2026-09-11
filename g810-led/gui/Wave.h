#ifndef WAVE_MODEL
#define WAVE_MODEL

#include <cstdint>
#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"

struct WavePoint {
	float t;
	float y;
	WavePoint() : t(0), y(0) {}
	WavePoint(float nt, float ny) : t(nt), y(ny) {}
};

struct WaveColorStop {
	float t;
	LedKeyboard::Color color;
	WaveColorStop() : t(0) { color.red = color.green = color.blue = 0xff; }
	WaveColorStop(float nt, LedKeyboard::Color nc) : t(nt), color(nc) {}
};

struct Wave {
	enum class Mode { pulse, travel };
	enum class Shape { sine, triangle, square, saw };

	Mode mode = Mode::pulse;
	Shape shape = Shape::sine;
	unsigned periodMs = 1000;
	float minY = 0.15f;
	float maxY = 1.0f;
	std::vector<WavePoint> points;
	std::vector<WaveColorStop> colors;

	void applyPreset();
	void sort();
	float brightnessAt(float t) const;
	LedKeyboard::Color colorAt(float t) const;
	LedKeyboard::Color sample(float t) const;
	std::string describe() const;
};

#endif
