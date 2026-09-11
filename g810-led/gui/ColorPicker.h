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

#ifndef GUI_COLOR_PICKER
#define GUI_COLOR_PICKER

#include <gtkmm.h>

#include <string>

// The colour chooser, with a way back out of its custom editor.
//
// GTK's chooser opens on a palette of presets with a row of the colours
// you have mixed before, and a "+" that swaps the whole dialog for the
// custom editor. From there the only buttons are Cancel — which throws
// the dialog away — and Select. Nothing goes back to the palette, so
// changing your mind about mixing a colour costs you the dialog.
//
// The page is the chooser's own "show-editor" property, which can be set
// both ways; all that was missing was a button to set it.
namespace colorpicker {

	// Modal. Returns false if the user cancelled; otherwise `color` is
	// what they chose.
	bool run(Gtk::Window *parent, const std::string &title, Gdk::RGBA &color);

}

// A colour button that opens the chooser above instead of the one GTK
// builds for itself. Everything else about it — the swatch it draws,
// get_rgba/set_rgba, the color-set signal — is GtkColorButton's.
class ColorPickButton : public Gtk::ColorButton {
	public:
		ColorPickButton();

	protected:
		// Deliberately does not chain up: the base class handler is what
		// opens the built-in dialog.
		void on_clicked() override;
};

#endif
