#ifndef WAVE_GRAPH
#define WAVE_GRAPH

#include <gtkmm.h>

#include "Wave.h"

class WaveGraph : public Gtk::DrawingArea {
	public:
		WaveGraph();

		void setWave(const Wave &wave);
		const Wave &getWave() const;
		void setWave(Wave &&wave) = delete;

		typedef sigc::signal<void> type_signal_changed;
		type_signal_changed signal_changed();

	protected:
		virtual bool on_draw(const Cairo::RefPtr<Cairo::Context> &context) override;

	private:
		bool onPress(GdkEventButton *event);
		bool onMotion(GdkEventMotion *event);
		bool onRelease(GdkEventButton *event);
		void toWave(double x, double y, float &t, float &brightness) const;
		void toPixel(float t, float brightness, double &x, double &y) const;
		int hitPoint(double x, double y) const;
		int hitColor(double x, double y) const;
		void emitChanged();

		Wave m_wave;
		int m_dragPoint = -1;
		int m_dragColor = -1;
		type_signal_changed m_signal_changed;
};

#endif
