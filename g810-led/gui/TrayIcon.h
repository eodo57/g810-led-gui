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

#ifndef TRAY_ICON
#define TRAY_ICON

#include <gtkmm.h>

#include <string>

// The status icon that stands in for the window while a live effect
// runs.
//
// It speaks StatusNotifierItem (through libayatana-appindicator), not
// GtkStatusIcon: the latter is X11 XEmbed and does nothing under
// Wayland, which is where this program is most likely to be used.
//
// Built without an appindicator library, or run on a desktop with no
// status-notifier host, every method here is a no-op and available()
// says so — the window must never hide behind an icon that will not
// appear, so callers ask first and keep their old behaviour otherwise.
class TrayIcon {
	public:
		TrayIcon();
		~TrayIcon();

		// Built with support *and* somewhere to put the icon. The
		// second half is a live question, so the answer is cached only
		// briefly rather than for the run.
		static bool available();

		// menu belongs to the caller and must outlive the icon; it is
		// exported to the desktop, which draws it itself.
		void show(Gtk::Menu &menu);
		void hide();
		bool visible() const;
		// Shown when the pointer rests on the icon.
		void setTooltip(const std::string &text);

	private:
		void *m_indicator;   // AppIndicator*, kept opaque so the header
		                     // stays free of the C library
		bool m_visible;
};

#endif
