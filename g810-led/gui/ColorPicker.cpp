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

#include "ColorPicker.h"

namespace {

// The custom editor is a widget inside the dialog, and which page is
// showing is exactly whether it is visible. It has no accessor, so it is
// found by type — the alternative, tracking the dialog's "show-editor"
// property, misses the "+" button, which switches pages without going
// through the property.
Gtk::Widget *findEditor(Gtk::Widget &widget) {
	if (G_OBJECT_TYPE_NAME(widget.gobj()) == std::string("GtkColorEditor"))
		return &widget;
	Gtk::Container *container = dynamic_cast<Gtk::Container*>(&widget);
	if (!container)
		return NULL;
	for (Gtk::Widget *child : container->get_children())
		if (child)
			if (Gtk::Widget *found = findEditor(*child))
				return found;
	return NULL;
}

const int RESPONSE_BACK = 1;

}  // namespace

bool colorpicker::run(Gtk::Window *parent, const std::string &title,
                      Gdk::RGBA &color) {
	Gtk::ColorChooserDialog dialog(title);
	if (parent)
		dialog.set_transient_for(*parent);
	dialog.set_use_alpha(false);
	dialog.set_rgba(color);

	Gtk::Button *back = Gtk::manage(new Gtk::Button("_Back"));
	back->set_use_underline(true);
	back->set_tooltip_text("Back to the palette");
	dialog.add_action_widget(*back, RESPONSE_BACK);
	// Left of Cancel and Select: it goes back rather than finishing.
	Gtk::ButtonBox *actions = dialog.get_action_area();
	if (actions)
		actions->set_child_secondary(*back, true);

	// Only while the editor is up — on the palette there is nowhere to go
	// back to, and a dead button is worse than no button.
	Gtk::Widget *editor = findEditor(dialog);
	sigc::slot<void> sync = [back, editor]() {
		back->set_visible(editor != NULL && editor->get_visible());
	};
	sigc::connection watch;
	if (editor)
		watch = editor->property_visible().signal_changed().connect(sync);
	back->show();
	sync();

	int response = Gtk::RESPONSE_CANCEL;
	do {
		response = dialog.run();
		if (response == RESPONSE_BACK) {
			g_object_set(dialog.gobj(), "show-editor", FALSE, NULL);
			sync();
		}
	} while (response == RESPONSE_BACK);
	watch.disconnect();

	if (response != Gtk::RESPONSE_OK)
		return false;
	color = dialog.get_rgba();
	return true;
}

ColorPickButton::ColorPickButton() {
	set_use_alpha(false);
}

void ColorPickButton::on_clicked() {
	Gdk::RGBA color = get_rgba();
	if (!colorpicker::run(dynamic_cast<Gtk::Window*>(get_toplevel()),
	                      "Pick a color", color))
		return;
	set_rgba(color);
	// GtkColorButton raises color-set from its own dialog, which is the
	// one thing here that is not being used; everything downstream still
	// expects the signal, so it is raised by hand.
	g_signal_emit_by_name(gobj(), "color-set");
}
