#include "Wave.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

static float wrap01(float t) {
	t = t - std::floor(t);
	if (t < 0)
		t += 1.0f;
	return t;
}

void Wave::applyPreset() {
	points.clear();
	switch (shape) {
		case Shape::sine:
			for (int i = 0; i <= 12; ++i) {
				float t = i / 12.0f;
				points.push_back({t, 0.5f + 0.5f * std::sin(t * 2.0f * 3.14159265f - 1.5707963f)});
			}
			break;
		case Shape::triangle:
			points.push_back({0.0f, 0.0f});
			points.push_back({0.5f, 1.0f});
			points.push_back({1.0f, 0.0f});
			break;
		case Shape::square:
			points.push_back({0.0f, 1.0f});
			points.push_back({0.49f, 1.0f});
			points.push_back({0.5f, 0.0f});
			points.push_back({1.0f, 0.0f});
			break;
		case Shape::saw:
			points.push_back({0.0f, 0.0f});
			points.push_back({0.99f, 1.0f});
			points.push_back({1.0f, 0.0f});
			break;
	}
	if (colors.empty())
		colors.push_back({0.0f, {0x44, 0x88, 0xff}});
	sort();
}

void Wave::sort() {
	std::sort(points.begin(), points.end(),
		[](const WavePoint &a, const WavePoint &b) { return a.t < b.t; });
	std::sort(colors.begin(), colors.end(),
		[](const WaveColorStop &a, const WaveColorStop &b) { return a.t < b.t; });
}

float Wave::brightnessAt(float t) const {
	t = wrap01(t);
	if (points.empty())
		return minY;
	if (points.size() == 1)
		return minY + (maxY - minY) * std::max(0.0f, std::min(1.0f, points[0].y));
	const WavePoint *a = &points.front();
	const WavePoint *b = &points.back();
	if (t <= points.front().t) {
		a = &points.back();
		b = &points.front();
		float span = 1.0f - a->t + b->t;
		float u = span <= 0 ? 0 : (t + 1.0f - a->t) / span;
		float y = a->y + (b->y - a->y) * u;
		return minY + (maxY - minY) * std::max(0.0f, std::min(1.0f, y));
	}
	if (t >= points.back().t) {
		a = &points.back();
		b = &points.front();
		float span = 1.0f - a->t + b->t;
		float u = span <= 0 ? 0 : (t - a->t) / span;
		float y = a->y + (b->y - a->y) * u;
		return minY + (maxY - minY) * std::max(0.0f, std::min(1.0f, y));
	}
	for (size_t i = 1; i < points.size(); ++i) {
		if (t <= points[i].t) {
			a = &points[i - 1];
			b = &points[i];
			break;
		}
	}
	float span = b->t - a->t;
	float u = span <= 0 ? 0 : (t - a->t) / span;
	float y = a->y + (b->y - a->y) * u;
	return minY + (maxY - minY) * std::max(0.0f, std::min(1.0f, y));
}

LedKeyboard::Color Wave::colorAt(float t) const {
	t = wrap01(t);
	if (colors.empty())
		return LedKeyboard::Color{0xff, 0xff, 0xff};
	if (colors.size() == 1)
		return colors[0].color;
	const WaveColorStop *a = &colors.back();
	const WaveColorStop *b = &colors.front();
	float u = 0;
	if (t < colors.front().t || t >= colors.back().t) {
		float span = 1.0f - colors.back().t + colors.front().t;
		u = span <= 0 ? 0 : (t >= colors.back().t ?
			(t - colors.back().t) / span :
			(t + 1.0f - colors.back().t) / span);
	} else {
		for (size_t i = 1; i < colors.size(); ++i) {
			if (t <= colors[i].t) {
				a = &colors[i - 1];
				b = &colors[i];
				float span = b->t - a->t;
				u = span <= 0 ? 0 : (t - a->t) / span;
				break;
			}
		}
	}
	LedKeyboard::Color color;
	color.red = (uint8_t)std::round(a->color.red + (b->color.red - a->color.red) * u);
	color.green = (uint8_t)std::round(a->color.green + (b->color.green - a->color.green) * u);
	color.blue = (uint8_t)std::round(a->color.blue + (b->color.blue - a->color.blue) * u);
	return color;
}

LedKeyboard::Color Wave::sample(float t) const {
	LedKeyboard::Color color = colorAt(t);
	float b = brightnessAt(t);
	color.red = (uint8_t)std::round(color.red * b);
	color.green = (uint8_t)std::round(color.green * b);
	color.blue = (uint8_t)std::round(color.blue * b);
	return color;
}

std::string Wave::describe() const {
	const char *modeName = mode == Mode::pulse ? "Pulse" : "Travel";
	const char *shapeName = "Sine";
	if (shape == Shape::triangle) shapeName = "Triangle";
	else if (shape == Shape::square) shapeName = "Square";
	else if (shape == Shape::saw) shapeName = "Saw";
	float hz = periodMs ? 1000.0f / periodMs : 0;
	char line[192];
	std::snprintf(line, sizeof(line),
		"%s %s · %.2f Hz (%u ms) · brightness %d–%d%% · %zu color stop%s",
		modeName, shapeName, hz, periodMs,
		(int)std::round(minY * 100), (int)std::round(maxY * 100),
		colors.size(), colors.size() == 1 ? "" : "s");
	return std::string(line);
}
