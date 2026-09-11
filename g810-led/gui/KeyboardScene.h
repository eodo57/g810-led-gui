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

#ifndef KEYBOARD_SCENE
#define KEYBOARD_SCENE

#include <gtkmm.h>

#include <map>
#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"
#include "KeyLayout.h"

// The keyboard as a rendered object: the layout extruded into keycaps on
// a plate, lit, turnable and zoomable.
//
// The geometry is generated from the same layout table the widget grid
// uses (gui/KeyLayout.h), in millimetres — a quarter unit is 19.05/4 —
// so it has the proportions of the real board, and of every other model
// the program supports, without shipping a model file that would only
// ever fit one of them.
//
// It knows nothing about painting, selection semantics or the draft: it
// draws what it is told and answers "which key is under this point".
// Everything else stays in KeyboardWidget, so both views behave the
// same because they run the same code.
class KeyboardScene : public Gtk::GLArea {
	public:
		KeyboardScene();
		~KeyboardScene();

		// True once a context exists and the shaders compiled. A view
		// that cannot start must hand back to the widget grid rather
		// than leave the editor blank.
		bool usable() const;
		// Emitted once the context has been realised (or failed), since
		// that only happens after the widget is shown.
		typedef sigc::signal<void> type_signal_void;
		type_signal_void signal_ready();

		void setCaps(const std::vector<keylayout::Cap> &caps);

		// What each cap shows. Colours are the LED colours; the cap
		// itself stays dark plastic, as it is on the desk.
		void setKeyColor(LedKeyboard::Key key, const Gdk::RGBA &color);
		void setKeyFlags(LedKeyboard::Key key, bool selected, bool pending);
		void setHovered(LedKeyboard::Key key, bool hovered);
		void clearHover();
		// The key the arrow keys are standing on. It shares the pointer's
		// slot in the shader but outranks it: hover steps aside when a
		// cap is already carrying two marks, because the pointer is
		// sitting on the key saying so, and a keyboard cursor has nothing
		// of the sort to fall back on.
		void setCursor(LedKeyboard::Key key, bool on);
		void clearCursor();

		// Which key is at this widget coordinate, by rendering the caps
		// with their index as a colour and reading the pixel back —
		// exact at any angle, and it cannot disagree with what is drawn.
		bool keyAt(double x, double y, LedKeyboard::Key &key);
		// Keys whose cap tops overlap this widget-space rectangle.
		std::vector<LedKeyboard::Key> keysIn(const Gdk::Rectangle &rect);
		// Where a cap is on screen, for the same question the grid
		// answers with widget allocations.
		bool keyRect(LedKeyboard::Key key, Gdk::Rectangle &rect);

		void orbit(double deltaX, double deltaY);
		void pan(double deltaX, double deltaY);
		void zoomBy(double steps);
		void resetView();

		// Renders into an offscreen buffer and reads it back, so tests
		// can look at the pixels without a window on screen.
		Glib::RefPtr<Gdk::Pixbuf> snapshot(int width, int height);

	protected:
		void on_realize() override;
		void on_unrealize() override;
		bool on_render(const Glib::RefPtr<Gdk::GLContext> &context) override;

	private:
		struct Vertex {
			float x, y, z;
			float nx, ny, nz;
			float u, v;
			float cap;       // index into the per-cap uniform arrays
		};
		struct CapEntry {
			keylayout::Cap spec;
			float color[3] = {0.06f, 0.06f, 0.07f};
			// Black or white, whichever can be read against the colour
			// above — the same decision styling::contrastingText makes
			// for the flat board's legends and for every swatch.
			float ink = 1.0f;
			bool selected = false;
			bool pending = false;
			bool hovered = false;
			bool cursor = false;
			// Cap top in model space, for projecting to the screen.
			float minX = 0, maxX = 0, minZ = 0, maxZ = 0, top = 0;
			// Where this cap's legend sits in the atlas, and how much of
			// the cap it covers, so a space bar's legend is the same
			// size as a letter's rather than stretched across it.
			float legend[4] = {0, 0, 0, 0};
		};

		void buildGeometry();
		// The legends, drawn once with Pango into one texture and kept
		// as a mask so the shader can light them in each key's colour.
		void buildAtlas();
		void buildPlate();
		// The points the outline of the board is made of: the plate's
		// corners and the top of every cap. The view is fitted to these
		// rather than to the box around them, because the box has corners
		// the board does not — a cap's height over the bare bezel at the
		// far edge — and fitting to those spends the stage on nothing.
		void buildHull();
		void uploadCapState();
		bool ensureProgram();
		void drawScene(int width, int height, bool picking);
		bool renderPick(int x, int y, int width, int height, int &capIndex);
		// How far back a camera at this angle has to stand for the whole
		// board to be inside a frustum with these half-tangents; the
		// distance that frames it in a viewport of this shape; and the
		// distance the camera stands at once the user's zoom is counted.
		// None of them is kept — they are worked out from the size passed
		// in, so a window that changes shape reframes the board without
		// anything having to notice.
		float distanceFor(float yaw, float pitch, float tanX, float tanY) const;
		// The same question asked of the outline rather than of the box.
		float fitOutline(float yaw, float pitch, float tanX, float tanY) const;
		// The distance that frames the outline, and the shift of the lens
		// that puts what is drawn in the middle of the stage instead of
		// the middle of the box around it.
		float frameFor(int width, int height, float &shiftX, float &shiftY) const;
		float fitDistance(int width, int height) const;
		float cameraDistance(int width, int height) const;
		// Where the camera stands and what it looks at; returns the
		// distance between the two. The matrix and the shading both need
		// this and must agree on it.
		float camera(int width, int height, float *eye, float *centre) const;
		void viewProjection(int width, int height, float *matrix) const;
		bool projectCap(const CapEntry &cap, int width, int height,
		                Gdk::Rectangle &rect) const;

		std::vector<keylayout::Cap> m_caps;
		std::vector<CapEntry> m_entries;
		std::map<LedKeyboard::Key, int> m_index;   // key -> entry
		float m_boardWidth = 0;    // millimetres
		float m_boardDepth = 0;
		// The outline, in millimetres about the point the camera aims at.
		std::vector<float> m_hull;
		// The last framing worked out, kept because a rubber band asks for
		// one per key per motion event and the answer only changes when
		// the angle or the viewport does.
		mutable float m_fitYaw = 1e9f, m_fitPitch = 1e9f;
		mutable int m_fitWidth = 0, m_fitHeight = 0;
		mutable float m_fitDistance = 0, m_fitShiftX = 0, m_fitShiftY = 0;
		// And the default view's distance, which turning the board may
		// never beat and which only the shape of the stage can change.
		mutable int m_homeWidth = 0, m_homeHeight = 0;
		mutable float m_homeDistance = 0;
		// The outline resolved into the view, kept only so that turning the
		// board does not allocate three times a frame.
		mutable std::vector<float> m_fitAlong, m_fitAcross, m_fitUpward;

		// Camera, in the usual orbit terms. The zoom is a factor on the
		// fitted distance rather than a length, so it survives the window
		// changing shape.
		float m_yaw = 0;
		float m_pitch = 0;
		float m_zoom = 1.0f;
		float m_panX = 0;
		float m_panY = 0;

		unsigned m_capProgram = 0;
		unsigned m_plateProgram = 0;
		unsigned m_capVao = 0, m_capVbo = 0;
		unsigned m_plateVao = 0, m_plateVbo = 0;
		unsigned m_atlas = 0;
		int m_atlasColumns = 0;
		unsigned m_pickFbo = 0, m_pickColor = 0, m_pickDepth = 0;
		int m_pickWidth = 0, m_pickHeight = 0;
		int m_capVertexCount = 0;
		// Flat quads that give the lamps a key-sized target; drawn in the
		// picking pass only.
		int m_pickVertexCount = 0;
		// And the strips of plate their names are printed on.
		int m_silkVertexCount = 0;
		int m_plateVertexCount = 0;
		bool m_usable = false;
		bool m_geometryDirty = true;
		type_signal_void m_signal_ready;
};

#endif
