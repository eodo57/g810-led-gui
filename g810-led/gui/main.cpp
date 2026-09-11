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

#include <gtkmm.h>

#include <cstring>

#include "AudioPlayer.h"
#include "MainWindow.h"
#include "Raindrop.h"
#include "Styling.h"

int main(int argc, char *argv[]) {
	// Daemon modes keep an animation running after the window closes;
	// they never touch GTK.
	if (argc >= 3 && std::strcmp(argv[1], "--rain-daemon") == 0)
		return runRainDaemon(argv[2]);
	if (argc >= 3 && std::strcmp(argv[1], "--audio-daemon") == 0)
		return runAudioDaemon(argv[2]);
	if (argc >= 3 && std::strcmp(argv[1], "--screen-daemon") == 0)
		return runScreenDaemon(argv[2]);

	Gtk::Main app(argc, argv);

	// The sheet, and the palette and class names it defines, are in
	// gui/Styling.h; it goes on at APPLICATION priority so it sits above
	// whatever theme the desktop is wearing.
	styling::installApplicationStyle();

	MainWindow window;
	window.show();
	// Not run(window): that form quits as soon as the window is hidden,
	// which is exactly what closing to the tray does. The window says
	// when it is really done (MainWindow::onDeleteEvent, quitFromTray).
	Gtk::Main::run();

	return 0;
}
