#include "WaveGraph.h"

#include "ColorPicker.h"

#include <cmath>

WaveGraph::WaveGraph() {
	set_size_request(220, 130);
	add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
	           Gdk::POINTER_MOTION_MASK);
	signal_button_press_event().connect(
		sigc::mem_fun(*this, &WaveGraph::onPress));
	signal_motion_notify_event().connect(
		sigc::mem_fun(*this, &WaveGraph::onMotion));
	signal_button_release_event().connect(
		sigc::mem_fun(*this, &WaveGraph::onRelease));
	m_wave.applyPreset();
}

void WaveGraph::setWave(const Wave &wave) {
	m_wave = wave;
	if (m_wave.points.empty())
		m_wave.applyPreset();
	m_wave.sort();
	queue_draw();
}

const Wave &WaveGraph::getWave() const {
	return m_wave;
}

WaveGraph::type_signal_changed WaveGraph::signal_changed() {
	return m_signal_changed;
}

void WaveGraph::emitChanged() {
	m_wave.sort();
	queue_draw();
	m_signal_changed.emit();
}

void WaveGraph::toWave(double x, double y, float &t, float &brightness) const {
	int w = get_allocated_width();
	int h = get_allocated_height();
	double left = 8, right = w - 8, top = 10, bottom = h - 18;
	t = (float)((x - left) / std::max(1.0, right - left));
	brightness = (float)((bottom - y) / std::max(1.0, bottom - top));
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	if (brightness < 0) brightness = 0;
	if (brightness > 1) brightness = 1;
}

void WaveGraph::toPixel(float t, float brightness, double &x, double &y) const {
	int w = get_allocated_width();
	int h = get_allocated_height();
	double left = 8, right = w - 8, top = 10, bottom = h - 18;
	x = left + t * (right - left);
	y = bottom - brightness * (bottom - top);
}

int WaveGraph::hitPoint(double x, double y) const {
	for (size_t i = 0; i < m_wave.points.size(); ++i) {
		double px = 0, py = 0;
		toPixel(m_wave.points[i].t, m_wave.points[i].y, px, py);
		if ((x - px) * (x - px) + (y - py) * (y - py) <= 64)
			return (int)i;
	}
	return -1;
}

int WaveGraph::hitColor(double x, double y) const {
	int h = get_allocated_height();
	for (size_t i = 0; i < m_wave.colors.size(); ++i) {
		double px = 0, py = 0;
		toPixel(m_wave.colors[i].t, 0, px, py);
		py = h - 8;
		if ((x - px) * (x - px) + (y - py) * (y - py) <= 64)
			return (int)i;
	}
	return -1;
}

bool WaveGraph::on_draw(const Cairo::RefPtr<Cairo::Context> &context) {
	int w = get_allocated_width();
	int h = get_allocated_height();
	context->set_source_rgb(0.12, 0.12, 0.12);
	context->rectangle(0, 0, w, h);
	context->fill();

	double left = 8, right = w - 8, top = 10, bottom = h - 18;
	double minY = bottom - m_wave.minY * (bottom - top);
	double maxY = bottom - m_wave.maxY * (bottom - top);
	context->set_source_rgba(1, 1, 1, 0.12);
	context->set_line_width(1);
	context->move_to(left, minY);
	context->line_to(right, minY);
	context->move_to(left, maxY);
	context->line_to(right, maxY);
	context->stroke();

	if (m_wave.points.size() >= 2) {
		context->set_source_rgb(0.4, 0.75, 1.0);
		context->set_line_width(1.8);
		bool first = true;
		for (int i = 0; i <= 64; ++i) {
			float t = i / 64.0f;
			float y = m_wave.points.empty() ? 0 : 0;
			if (!m_wave.points.empty()) {
				float raw = 0;
				if (m_wave.maxY > m_wave.minY)
					raw = (m_wave.brightnessAt(t) - m_wave.minY) /
					      (m_wave.maxY - m_wave.minY);
				else
					raw = m_wave.minY;
				y = std::max(0.0f, std::min(1.0f, raw));
			}
			double x = 0, py = 0;
			toPixel(t, y, x, py);
			if (first)
				context->move_to(x, py);
			else
				context->line_to(x, py);
			first = false;
		}
		context->stroke();
	}

	for (const WavePoint &point : m_wave.points) {
		double x = 0, y = 0;
		toPixel(point.t, point.y, x, y);
		context->set_source_rgb(0.85, 0.9, 1.0);
		context->arc(x, y, 4, 0, 6.28318);
		context->fill();
	}

	for (const WaveColorStop &stop : m_wave.colors) {
		double x = 0, y = 0;
		toPixel(stop.t, 0, x, y);
		y = h - 8;
		context->set_source_rgb(stop.color.red / 255.0,
		                        stop.color.green / 255.0,
		                        stop.color.blue / 255.0);
		context->arc(x, y, 5, 0, 6.28318);
		context->fill();
		context->set_source_rgb(1, 1, 1);
		context->set_line_width(1);
		context->arc(x, y, 5, 0, 6.28318);
		context->stroke();
	}
	return true;
}

bool WaveGraph::onPress(GdkEventButton *event) {
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		int point = hitPoint(event->x, event->y);
		if (point >= 0 && (int)m_wave.points.size() > 2) {
			m_wave.points.erase(m_wave.points.begin() + point);
			emitChanged();
			return true;
		}
		int color = hitColor(event->x, event->y);
		if (color >= 0 && (int)m_wave.colors.size() > 1) {
			m_wave.colors.erase(m_wave.colors.begin() + color);
			emitChanged();
			return true;
		}
		return true;
	}
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		float t = 0, y = 0;
		toWave(event->x, event->y, t, y);
		Gdk::RGBA rgba("#ff0000");
		if (colorpicker::run(dynamic_cast<Gtk::Window*>(get_toplevel()),
		                     "Wave color stop", rgba)) {
			WaveColorStop stop;
			stop.t = t;
			stop.color.red = (uint8_t)std::round(rgba.get_red() * 255.0);
			stop.color.green = (uint8_t)std::round(rgba.get_green() * 255.0);
			stop.color.blue = (uint8_t)std::round(rgba.get_blue() * 255.0);
			m_wave.colors.push_back(stop);
			emitChanged();
		}
		return true;
	}
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		int color = hitColor(event->x, event->y);
		if (color >= 0) {
			m_dragColor = color;
			return true;
		}
		int point = hitPoint(event->x, event->y);
		if (point >= 0) {
			m_dragPoint = point;
			return true;
		}
		float t = 0, y = 0;
		toWave(event->x, event->y, t, y);
		m_wave.points.push_back({t, y});
		m_wave.sort();
		m_dragPoint = hitPoint(event->x, event->y);
		emitChanged();
		return true;
	}
	return false;
}

bool WaveGraph::onMotion(GdkEventMotion *event) {
	float t = 0, y = 0;
	toWave(event->x, event->y, t, y);
	if (m_dragPoint >= 0 && m_dragPoint < (int)m_wave.points.size()) {
		m_wave.points[m_dragPoint].t = t;
		m_wave.points[m_dragPoint].y = y;
		emitChanged();
		// emitChanged() sorts by t, so the index no longer names the
		// point the user grabbed as soon as it crosses a neighbour —
		// follow it by value instead, or the drag jumps to another point.
		for (size_t i = 0; i < m_wave.points.size(); ++i) {
			if (m_wave.points[i].t == t && m_wave.points[i].y == y) {
				m_dragPoint = (int)i;
				break;
			}
		}
		return true;
	}
	if (m_dragColor >= 0 && m_dragColor < (int)m_wave.colors.size()) {
		LedKeyboard::Color dragged = m_wave.colors[m_dragColor].color;
		m_wave.colors[m_dragColor].t = t;
		emitChanged();
		for (size_t i = 0; i < m_wave.colors.size(); ++i) {
			if (m_wave.colors[i].t == t &&
			    m_wave.colors[i].color.red == dragged.red &&
			    m_wave.colors[i].color.green == dragged.green &&
			    m_wave.colors[i].color.blue == dragged.blue) {
				m_dragColor = (int)i;
				break;
			}
		}
		return true;
	}
	return false;
}

bool WaveGraph::onRelease(GdkEventButton *event) {
	if (event->button == 1) {
		m_dragPoint = -1;
		m_dragColor = -1;
		return true;
	}
	return false;
}
