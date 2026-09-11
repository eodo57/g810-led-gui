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

#ifndef GUI_STYLING
#define GUI_STYLING

#include <gtkmm.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// Painting a widget with an arbitrary colour, the way GTK3 still supports.
//
// override_background_color() does the same job in one call but has been
// deprecated since 3.16, and it silently loses to any theme rule that is
// more specific — so a themed build could ignore the colour the user just
// picked. A provider attached to the widget's own style context wins
// instead, and only its data changes when the colour does.
namespace styling {

	inline std::string hex(const Gdk::RGBA &color) {
		char buffer[8];
		std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x",
			(int)(color.get_red() * 255.0 + 0.5),
			(int)(color.get_green() * 255.0 + 0.5),
			(int)(color.get_blue() * 255.0 + 0.5));
		return std::string(buffer);
	}

	// An sRGB channel and the linear light behind it. Contrast is a
	// question about light, and the numbers a colour is written in are
	// not light; every rule in this file that weighs one colour against
	// another goes through these two first.
	inline double decode(double channel) {
		return channel <= 0.03928 ? channel / 12.92 :
			std::pow((channel + 0.055) / 1.055, 2.4);
	}

	inline double encode(double channel) {
		channel = std::max(0.0, std::min(1.0, channel));
		return channel <= 0.0031308 ? channel * 12.92 :
			1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
	}

	// WCAG relative luminance: the sRGB channels have to be linearised
	// first. A plain weighted average of the raw channels gets saturated
	// mid-tones wrong — it puts white text on magenta at 2.8:1.
	inline double luminance(const Gdk::RGBA &color) {
		return 0.2126 * decode(color.get_red()) +
		       0.7152 * decode(color.get_green()) +
		       0.0722 * decode(color.get_blue());
	}

	inline double contrastRatio(const Gdk::RGBA &a, const Gdk::RGBA &b) {
		double high = luminance(a), low = luminance(b);
		if (high < low)
			std::swap(high, low);
		return (high + 0.05) / (low + 0.05);
	}

	// Whichever label colour is actually more readable on this keycap,
	// rather than guessing from a brightness threshold. Pure black and
	// white are the candidates deliberately: at the crossover between
	// them the ratio bottoms out at sqrt(1.05/0.05) = 4.58:1, so every
	// possible LED colour still clears the 4.5:1 readability floor —
	// softer near-black/near-white pairs dip below it for mid blues.
	inline std::string contrastingText(const Gdk::RGBA &color) {
		static const char *dark = "#000000";
		static const char *light = "#ffffff";
		return contrastRatio(color, Gdk::RGBA(dark)) >=
		       contrastRatio(color, Gdk::RGBA(light)) ? dark : light;
	}

	// What colour to draw a mark in on a keycap of this colour.
	//
	// This is the one rule both views of the board mark state by, and it
	// exists because a mark declared as a constant cannot be seen on a
	// key painted that constant. The flat board once said
	// `border-color: @kb_amber` in the sheet and the model ran the same
	// amber through this; paint every key #f5c211 and the flat board's
	// selection vanished while the model's stayed. CSS cannot fix that,
	// because CSS does not know what colour the key underneath is. Both
	// views now ask here, and both get the same answer.
	//
	// The mark keeps the hue the rest of the window uses for that state
	// and gives up value instead, and only as much of it as it takes to
	// be seen. How much that is depends on the key underneath: two hues
	// as far apart as gold and red are told apart by the hue alone and
	// are left alone, while a hue laid on a key painted that same hue
	// says nothing at all and has to clear three to one on brightness by
	// itself.
	//
	// What is asked for is a contrast, not a colour, because the road
	// from white to a dark grey runs straight through the value of the
	// cap it has to be seen against: a mark moved half way would be less
	// visible than one not moved at all. Down is tried first — a dark
	// gold on a bright gold key still reads as gold — and the mark is
	// lifted towards white only where there is no room below.
	//
	// The move is made in linear light, so the ratio it lands on is the
	// ratio that was asked for rather than one near it. KeyboardScene's
	// fragment shader carries the same function in GLSL, word for word;
	// changing one means changing the other.
	inline Gdk::RGBA markColor(const Gdk::RGBA &hue, const Gdk::RGBA &cap) {
		const double hueLum = luminance(hue), capLum = luminance(cap);
		const double have = (std::max(hueLum, capLum) + 0.05) /
			(std::min(hueLum, capLum) + 0.05);
		const double channels[3] = {
			hue.get_red(), hue.get_green(), hue.get_blue()
		};
		// How far apart the two colours are in hue alone, with value
		// divided out: gold on red is a long way, gold on gold is nowhere.
		const double hueTop = std::max(0.004, std::max(channels[0],
			std::max(channels[1], channels[2])));
		const double capTop = std::max(0.004, std::max(cap.get_red(),
			std::max(cap.get_green(), cap.get_blue())));
		const double capChannels[3] = {
			cap.get_red(), cap.get_green(), cap.get_blue()
		};
		double apart = 0.0;
		for (int i = 0; i < 3; ++i) {
			const double step = channels[i] / hueTop - capChannels[i] / capTop;
			apart += step * step;
		}
		const double reach = std::min(1.0, std::sqrt(apart) / 0.65);
		const double want = 3.0 + (1.5 - 3.0) * reach;
		if (have >= want)
			return hue;
		// Down by scaling the light, or up by mixing white into it —
		// whichever there is room for, aimed at the luminance the wanted
		// ratio works out to.
		const double lower = (capLum + 0.05) / want - 0.05;
		const double raise = (capLum + 0.05) * want - 0.05;
		const double scale = lower > 0.015 ?
			std::min(1.0, lower / std::max(hueLum, 1e-4)) : 1.0;
		const double lift = lower > 0.015 ? 0.0 : std::max(0.0, std::min(1.0,
			(raise - hueLum) / std::max(1.0 - hueLum, 1e-3)));
		double moved[3];
		for (int i = 0; i < 3; ++i) {
			const double light = decode(channels[i]) * scale;
			moved[i] = encode(light + (1.0 - light) * lift);
		}
		Gdk::RGBA answer;
		answer.set_rgba(moved[0], moved[1], moved[2], 1.0);
		return answer;
	}

	// Attaches (once) and updates a provider that sets only the text
	// colour. For widgets that draw their own background: reloading a
	// provider is what costs, so it is worth doing only when the value
	// it carries has actually changed.
	inline void paintText(Gtk::Widget &widget,
	                      Glib::RefPtr<Gtk::CssProvider> &provider,
	                      const std::string &color) {
		if (!provider) {
			provider = Gtk::CssProvider::create();
			widget.get_style_context()->add_provider(provider,
				GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		}
		try {
			provider->load_from_data("* { color: " + color + "; }");
		} catch (const Glib::Error &error) {
			// A malformed colour should never blank the board.
		}
	}

	// Attaches (once) and updates a provider that paints this widget.
	inline void paint(Gtk::Widget &widget,
	                  Glib::RefPtr<Gtk::CssProvider> &provider,
	                  const Gdk::RGBA &color, bool withText = true) {
		if (!provider) {
			provider = Gtk::CssProvider::create();
			widget.get_style_context()->add_provider(provider,
				GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		}
		std::string css = "* { background-color: " + hex(color) + ";";
		if (withText)
			css += " color: " + contrastingText(color) + ";";
		css += " }";
		try {
			provider->load_from_data(css);
		} catch (const Glib::Error &error) {
			// A malformed colour should never blank the board.
		}
	}

	// The whole window's style sheet, installed by gui/main.cpp. It lives
	// here rather than in main() so that a test harness which builds a
	// MainWindow directly can put the same chrome on it and be looking at
	// the real thing.
	inline const char *applicationCss() {
		return R"CSS(
/* ==========================================================================
   g810-led — the window's style sheet.

   Dark on purpose. This window's whole job is showing colour, and a light
   chrome throws a cast over every swatch and over the rendered board: a
   mid-blue LED next to beige is not the mid-blue that will be on the desk.
   The surfaces below are near-neutral (a trace of blue, no more) so that
   what is judged against them is the colour itself.

   ---- The vocabulary the panels use ---------------------------------------

     Structure
       .kb-card       a section: one group of controls, on its own surface.
                      A plain Gtk::Frame gets this already, so addSection()
                      needs no change. `.kb-section` is the same thing.
       .kb-toolbar    a strip of peer controls that belongs to the surface
                      under it. Tinted from the text colour, so it reads
                      the same on a card, on the sheet or on the window.
       .kb-panel      a column of cards. Transparent.
       .kb-well       a recessed area: a list, a log, a preview.
       .kb-stage      the surface the board is shown on.

     Type — five levels off three sizes and three greys
                      At the default 15px, and against the card (#23272e):

       .kb-heading    1.28em  19px  700  #ffffff  15.0:1
                      The one line that names what you are looking at.
       .kb-title      0.82em  12px  700  #c3ccd9   9.3:1 + 1.1px tracking
                      + a grey tick down its left edge. The name of a
                      card: a label for a group, deliberately quieter
                      than the controls in it. Also `frame > label`.
       (body)         1.00em  15px  400  #e8ecf2  12.6:1
       .kb-subtitle   1.00em  15px  400  #a9b2c0   7.0:1
                      The line under a heading — a sentence, so it is set
                      at the size sentences are set at here and made
                      secondary by colour, not by being shaved a point
                      smaller than the body. Also `.dim-label`.
       .kb-hint       0.86em  13px  400  #a9b2c0   7.0:1
                      What a control does, why it is off, which lane a
                      tab is. Never a whole sentence of orientation.
       .kb-status     the running commentary along the foot.
       .kb-numeric    monospaced, for values that tick.

                      What separates each pair, honestly stated. Heading
                      to title: 7px, two greys, the weight and the
                      tracking — this is the step the whole hierarchy
                      hangs off, and it used to be nothing at all
                      (12px bold near-white against 14px bold
                      near-white, with a third level in between at the
                      same weight and colour). Title to body: 3px and
                      the weight. Body to subtitle: colour alone — they
                      are the same sentence at the same size, one of
                      them secondary, which is what "secondary" means.
                      Subtitle to hint: 2px, and it is the softest step
                      in the scale; the two are never set on adjacent
                      lines.

     Emphasis
       .kb-primary    the one action a screen exists for: filled in the
                      accent, so the eye lands on it before it reads
                      anything. One per screen — and it is filled
                      wherever it is put, including inside a tab.
                      `.suggested-action` is a synonym.
       .kb-secondary  the accent as an outline rather than a fill, for
                      the rare screen with a genuine pair of near-equal
                      actions. Never applied automatically.
       .kb-danger     throws work away. Outlined in red, not filled: it
                      should be findable, not tempting.
                      `.destructive-action` is a synonym.
       .kb-quiet      a diagnostic or developer control. Present,
                      unadvertised.
       .kb-swatch     a control whose whole content is a colour. Framed
                      by two hairlines, light outside and dark inside, so
                      that black reads as a colour and not as a hole in
                      the window, and white does not run into the card.
       .kb-field      a custom widget that should read as somewhere you
                      type, the way an entry does.

     The board
       .kb-board      the keyboard itself.
       .kb-key        a keycap.  .kb-led  an indicator cap.
       .kb-selected   chosen (amber ring).
       .kb-pending    holds a colour that has not been sent (accent ring,
                      the same blue as the Send button, because that
                      button is what resolves it).
     These last two carry no appearance of their own. They are state,
     read by KeyCap::on_draw, which draws the ring through
     styling::markColor — the same rule the model's shader marks by, and
     the only one that can see the colour of the key it is marking.

   ---- One way to say "this one is on" -------------------------------------

   A tab, a segment of the Paint/Select control and any other toggle all
   say it identically: a wash of the accent behind the label, the label
   lifted to white, and a 2px accent bar along the bottom edge. Nothing in
   this window uses a grey fill to mean "on" — grey means "off" here, and
   a control cannot mean both. Focus is a different mark again, and never
   a wash: a 3px accent band inside the control's own edge, the same band
   on every kind of control there is.

   ---- Surfaces ------------------------------------------------------------

   Four levels, each a step lighter than the one behind it, each with a
   hairline where the step alone is too small to see:
     window @kb_root -> sheet @kb_sheet -> card @kb_surface -> control
   The board sits on @kb_sunken, which is the exact colour the 3D scene
   clears to (KeyboardScene::drawScene), so the stage and the rendered
   board are one surface with no seam.

   ---- Spacing -------------------------------------------------------------

   Three steps and nothing between them. 4px is the atom: a control's own
   vertical padding, the gap between siblings. 8px is the next step: the
   sides of a field, the room above and below a status line, the gutter
   around a card, around the sheet and around the stage. 12px is the
   indent inside a card and the room either side of a button's label.
   Height comes from min-height — 24px of content in a button or a field,
   so 34px on screen — rather than from more padding, so a row of mixed
   controls lines up. Radii: 4px on a keycap (KeyboardWidget draws its
   fills to match), 6px on anything you click into, 10px on anything that
   holds other things, and 14px on the stage, which is wide enough that a
   smaller corner would not read as a corner at all.

   ---- Contrast ------------------------------------------------------------

   Every number below was printed by styling::contrastRatio(), the same
   function the window itself uses to pick a legend colour, and every
   surface fed to it was read back off a screenshot of the running window
   rather than taken from the definition above — a wash is alpha() over
   whatever happens to be behind it, and what GTK composites and what the
   arithmetic predicts differ by a point or two. Text clears 4.5:1
   everywhere; anything that is the boundary of a control clears 3:1 on
   both sides of itself.

     Text, on the card it sits on (#23272e)
       body      #e8ecf2  12.6:1     heading  #ffffff  15.0:1
       title     #c3ccd9   9.3:1     accent   #66ccff   8.3:1
       hint,
       subtitle  #a9b2c0   7.0:1     danger   #ff8172   6.2:1
       disabled  #9aa3b1   5.9:1
     Text, on the sheet (#1b1e23) and the window (#14161a)
       body 14.1:1 / 15.3:1  title 10.3:1 / —  dim 7.8:1 / 8.5:1
       a disabled icon, #9aa3b1 on the window, 7.1:1
     Text on a filled or tinted control
       on the accent fill        #08131c on #66ccff  10.4:1
       on the chosen tab         #ffffff on #24323e  13.1:1
       on the chosen segment     #ffffff on #2b404c  10.8:1
       on the toolbar            #e8ecf2 on #1f2125  13.6:1
     Text on a control that is switched off — the case that used to fail
       on a combo or a field that keeps its raised fill (#2d323a)  5.1:1
       on the tint the primary keeps with nothing to send (#2a3742) 4.8:1
     Component edges — WCAG asks 3:1 of a boundary, against both sides
       @kb_edge  #737c8b  3.6:1 on a card, 3.1:1 on a control's own fill,
                          4.0:1 on the sheet, 4.3:1 on the window,
                          4.5:1 on the board — the rim of the stage
       accent    #66ccff  8.3:1 on a card, 7.2:1 on a control's own fill,
                          9.3:1 on the sheet, 10.0:1 on the window,
                          7.3:1 as the bar under the chosen tab.
                          This is the focus ring as well, so the ring is
                          the best-lit thing in the sheet — which is what
                          you want of the one mark a keyboard-only reader
                          navigates by.
       on the primary fill, where an accent ring would vanish, the ring
                          is #08131c instead:                     10.4:1
       amber     #f5c211  9.0:1 on a card, 11.3:1 on the board
       the outline the primary keeps when it is off  (#4d8db0)
                          4.1:1 on the card, 3.3:1 on its own tint
       the handle between the two halves of the window (#656c7a) 3.4:1
       the rule over the status line   (#484e58)  2.2:1 — a divider, not
                          a control, so it sits between the two floors
       the error bar's edge (#b3594c) 3.2:1 on its own tint, 3.8:1 on the
                          window; its text #ffd7d0 on that tint, 11.4:1
   Hairlines (@kb_line, 1.3:1) are deliberately under that floor: they
   separate, they do not carry meaning, and a card whose outline shouts is
   a card you read before its contents. Every control that needs a
   boundary has @kb_edge as well.

   The one pair that cannot be fixed by choosing a colour is the stage
   against the window: 1.04:1, because its floor has to be the exact
   colour KeyboardScene clears its viewport to or the rendered board sits
   on a visible seam. The stage is therefore drawn by its rim (4.3:1
   against the window, 4.5:1 against its own floor) and by a shadow cast
   inwards from it, not by its fill.

   Not carried by colour alone: "on" adds a bar under the label, the
   primary adds weight and a fill, the danger adds an outline, focus adds
   a 3px band inside the edge, and the two rings on the board — amber for
   chosen, accent for unsent — are both stated in words as well, in the
   strip and in the count beside it.
   ========================================================================== */

@define-color kb_root      #14161a;   /* behind everything */
@define-color kb_sheet     #1b1e23;   /* the tabbed body: one large surface */
@define-color kb_surface   #23272e;   /* cards */
@define-color kb_raised    #2d323a;   /* buttons, fields */
@define-color kb_raised_hi #383e48;   /* the same, under the pointer */
@define-color kb_sunken    #101216;   /* the stage, troughs, wells */
@define-color kb_line      #333944;   /* hairlines between things */
@define-color kb_edge      #737c8b;   /* the edge of a control */
/* The edge of a control that cannot be pressed. Dimmer than kb_edge, so
   the two states are told apart at a glance, but still drawn: at the
   hairline grey this used to borrow (#333944, 1.6:1 on the window) a
   disabled button lost its outline entirely and read as a stray label
   lying on the background. Half the buttons in this window are switched
   off at rest — Send, Discard, Deselect, Turn off — so "switched off"
   has to look like a state of a button and not like the absence of one.
   2.6:1 on a card, 3.2:1 on the window, against 3.6:1 and 4.1:1 for the
   live edge. */
@define-color kb_edge_off  #5f6775;
@define-color kb_text      #e8ecf2;
@define-color kb_text_hi   #ffffff;
@define-color kb_text_dim  #a9b2c0;
/* A card's title, one step up from the hint under it so the two are
   different levels and not the same grey twice. */
@define-color kb_title     #c3ccd9;
/* Off, not absent. A disabled label still has to be read — it is how you
   find out what the control you cannot press would have done — so this is
   the lightest grey that still reads as "not now" while clearing 4.5:1 on
   every surface a disabled label lands on, including the raised fill of a
   combo and the tint the primary keeps when it has nothing to send. */
@define-color kb_text_off  #9aa3b1;
@define-color kb_accent    #66ccff;
@define-color kb_on_accent #08131c;
@define-color kb_danger    #ff8172;
@define-color kb_danger_dk #b3594c;
@define-color kb_amber     #f5c211;
@define-color kb_cap       #2f343c;   /* an unlit keycap */
@define-color kb_cap_led   #3d434d;   /* an indicator cap, a shade up */
@define-color kb_cap_off   #22262c;   /* a cap this keyboard does not have */
/* The keyboard's own top plate, which the flat board draws under its keys
   and the model extrudes. It is the mid grey the model's brushed
   aluminium actually averages to on screen — measured off a rendered
   frame, not guessed — so switching views does not change the colour of
   the thing you are looking at. It is a surface in a picture, not a
   surface of this window: nothing in the chrome is this colour, and
   nothing should be. White reads on it at 5.1:1, which is what the lamps'
   names are silkscreened in. */
@define-color kb_plate     #6b6e75;

/* The stock names, so anything drawn by GTK itself rather than by a rule
   below — a colour chooser, a file chooser, a print dialog — comes with
   us instead of staying beige. */
@define-color theme_bg_color @kb_surface;
@define-color theme_fg_color @kb_text;
@define-color theme_base_color @kb_sunken;
@define-color theme_text_color @kb_text;
@define-color theme_selected_bg_color @kb_accent;
@define-color theme_selected_fg_color @kb_on_accent;
@define-color insensitive_bg_color @kb_surface;
@define-color insensitive_fg_color @kb_text_off;
@define-color borders @kb_edge;
@define-color theme_unfocused_bg_color @kb_surface;
@define-color theme_unfocused_fg_color @kb_text;

/* --- Surfaces ---------------------------------------------------------- */

window, dialog, .background, messagedialog {
	background-color: @kb_root;
	color: @kb_text;
}
/* No window of this application has one, but a stock dialog gets one
   whenever the desktop asks for it, and a theme's header bar is the one
   surface that would then arrive in the theme's own colour on top of a
   window in ours. */
headerbar, headerbar.titlebar {
	background-image: none;
	background-color: @kb_surface;
	color: @kb_text;
	border-bottom: 1px solid @kb_line;
	box-shadow: none;
}
headerbar label, headerbar .title { color: @kb_text; }
headerbar .subtitle { color: @kb_text_dim; }

/* Anything that floats over the window is a raised surface with an edge —
   the same treatment a menu gets. Without the edge a popover shares its
   colour with the window it is covering and stops looking like a separate
   thing you can dismiss. */
popover, popover.background {
	background-color: @kb_raised;
	color: @kb_text;
	border: 1px solid @kb_edge;
	border-radius: 10px;
}
popover > arrow {
	background-color: @kb_raised;
	border: 1px solid @kb_edge;
}
/* A row inside a popover is a widget in its own right, not a run of text,
   so it does not inherit the colour set above: left alone it keeps the
   theme's near-black label and disappears into the dark fill it is drawn
   on. Every node that can carry a colour in there is named. */
popover modelbutton, popover modelbutton.flat, popover modelbutton label,
popover > contents > modelbutton, popover button label {
	color: @kb_text;
	background-color: transparent;
	background-image: none;
	text-shadow: none;
}
popover modelbutton:hover, popover modelbutton.flat:hover {
	background-color: @kb_accent;
	color: @kb_on_accent;
}
popover modelbutton:hover label { color: @kb_on_accent; }
popover modelbutton:disabled, popover modelbutton:disabled label {
	color: @kb_text_off;
}
popover modelbutton arrow, popover modelbutton check,
popover modelbutton radio { color: @kb_text_dim; }

/* Two vertical bands: the board on the left, the controls on the right.
   The handle between them is a line you can grab, not a gap. It is the
   only thing saying where one half of the window ends, and it can be
   dragged, so it is lit well above the hairline a card uses: 3.4:1
   against the window on both sides of it, which is the floor a control
   owes its surroundings. A card's outline stays under that floor on
   purpose — a card is scenery, this is a control.

   What it looks like and what you can hit are two different sizes. The
   window asks for a wide handle (Gtk::Paned::set_wide_handle) and this
   used to state min-width: 1px on the same node, which silently won: the
   only way to rebalance the board against the controls was a one-pixel
   target you had to hunt for. The node is now the full handle and the
   line is painted down the middle of it as a background image, so the
   thing you see is still a hairline and the thing you hit is eleven
   pixels wide. Under the pointer the line thickens and turns accent,
   which is the only cue that it was ever draggable. */
paned > separator {
	background-color: transparent;
	background-image: image(alpha(@kb_edge, 0.85));
	background-repeat: no-repeat;
	background-position: center;
	background-size: 1px 100%;
	min-width: 11px;
	min-height: 11px;
}
paned > separator:hover {
	background-image: image(@kb_accent);
	background-size: 3px 100%;
}
paned.vertical > separator { background-size: 100% 1px; }
paned.vertical > separator:hover { background-size: 100% 3px; }

scrolledwindow, viewport, .kb-panel { background-color: transparent; }

.kb-well {
	background-color: @kb_sunken;
	color: @kb_text;
	border: 1px solid @kb_line;
	border-radius: 10px;
}
treeview.view, textview, textview text, iconview, list {
	background-color: @kb_sunken;
	color: @kb_text;
}
treeview.view:selected, list > row:selected, textview text selection,
selection {
	background-color: @kb_accent;
	color: @kb_on_accent;
}

/* --- Type -------------------------------------------------------------- */

/* There is deliberately no blanket `label { color: ... }`. Text colour is
   inherited, and a keycap's legend colour is set on the cap itself
   (styling::paintText, which picks black or white for readability against
   the LED colour underneath). A rule matching the legend's own node would
   win over that inheritance and put pale grey on a yellow key. */
label:disabled { color: @kb_text_off; opacity: 1; }
.kb-key label, .kb-led label { color: inherit; opacity: 1; }

/* Level 1. Bigger, heavier and whiter than anything else on the screen,
   and there is one of it. */
.kb-heading {
	font-size: 1.28em;
	font-weight: 700;
	color: @kb_text_hi;
}

/* Level 2. The name of a card is a label for a group, not a headline: a
   card already has an outline saying where it starts, so its title can be
   small and tracked and still be found instantly. Seven of these on a
   screen have to sit under the content they name, not shout over it —
   which is what happens when every title is bold near-white 15px.
   addSection() marks the title up as <b>, and a Pango attribute beats CSS
   font-weight, so the weight here is a statement of intent rather than
   the thing doing the work; the size, the tracking, the tick and the
   colour are.

   It has its own colour rather than sharing the hint's. In every card a
   title sits directly on top of a hint — "Startup mode" over "What the
   keyboard shows on its own…" — and when both are the same grey the pair
   reads as one texture instead of as a label and its explanation. This is
   a step lighter (9.3:1 on a card against the hint's 7.0:1), and it
   carries a short tick down its left edge (#7b828c composited, 3.9:1 on
   a card) so the level survives for a reader who cannot tell the two
   greys apart.

   The tick is grey, not accent. It was accent, and on a screen with four
   cards on it that put four blue marks in the chrome before the eye had
   found the one blue mark that means something — the button. Blue in
   this window is reserved for three things and they are all live: the
   action, what is switched on, and what has keyboard focus. A tick that
   labels a heading is none of them.

   Small caps would say the level better still, but text-transform is not
   a property GTK 3.24 has, so that would have to come from the strings
   the panels pass in. */
.kb-title, .kb-section-title, frame > label {
	font-size: 0.82em;
	font-weight: 700;
	letter-spacing: 1.1px;
	color: @kb_title;
	border-left: 2px solid alpha(@kb_title, 0.55);
	padding: 2px 12px 2px 8px;
	margin: 8px 0 2px 0;
}

/* Level 4: the line under a heading, and the running note under the tab
   strip. It is body size on purpose — it is a sentence about what you are
   looking at, and a sentence set a hair smaller than the body only looks
   like body text that went wrong. What makes it secondary is the colour.
   Level 5, the hint, is where the size drops.

   .dim-label is aliased here rather than to the hint. It is the stock
   class a panel reaches for when it wants "this line is secondary", and
   every sentence in this window that carries it is a sentence, not a
   caption — routing it to 0.86em is how the two most prominent orienting
   lines in the app ended up being the smallest text on it. The stock
   class also fades the widget to 55% opacity, which on this background
   lands under 4:1; a stated colour instead, so the ratio in the header
   comment is the ratio on screen. */
.kb-subtitle, .dim-label, label.dim-label {
	font-size: 1em;
	color: @kb_text_dim;
	opacity: 1;
}
/* Level 5. What a control does, why it is off, which lane a tab is —
   never a whole sentence of orientation.

   0.86em, which at the default 15px is a two-pixel step down from the
   line above. 0.92 was not: a subtitle and a hint in the same grey one
   pixel apart are one level wearing two names, and this window puts them
   within a few lines of each other — "Staged — press Send to keyboard…"
   over "Nothing selected — drag across the board…" — where the eye can
   compare them directly and finds nothing to compare. */
.kb-hint {
	font-size: 0.86em;
	color: @kb_text_dim;
	opacity: 1;
}
.kb-hint:disabled, .dim-label:disabled, .kb-subtitle:disabled {
	color: @kb_text_off;
}

.kb-numeric { font-family: monospace; }

/* The left column of the shortcut list: what you press, or what you do
   with the mouse. That column is scanned rather than read, so it is the
   brightest text on its row and the sentence beside it is ordinary body —
   the same two levels a card title and its controls have, turned on their
   side. */
.kb-shortcut {
	font-weight: 700;
	color: @kb_text_hi;
}

/* --- Cards ------------------------------------------------------------- */

/* A card carries its own margin, so there is nothing at the edge left for
   a viewport to clip however narrow the window gets. */
.kb-card, .kb-section, frame {
	background-color: @kb_surface;
	border: 1px solid @kb_line;
	border-radius: 10px;
	margin: 8px 8px 0 8px;
	padding: 0;
}
frame > border { border-style: none; }
.kb-card > box, .kb-card > grid, .kb-section > box, .kb-section > grid,
frame > box, frame > grid {
	padding: 0 4px 12px 4px;
}

/* One frame in this window is not a card: the one GtkStatusbar keeps
   inside itself, which groups nothing and is pure plumbing. */
statusbar > frame {
	background-color: transparent;
	border: none;
	margin: 0;
	padding: 0;
}
statusbar > frame > border { border-style: none; }

/* A row of peers. Tinted from the text colour rather than stated flat, so
   the same strip reads correctly on the window, on the sheet and on a
   card without three variants of it. */
.kb-toolbar {
	background-color: alpha(@kb_text, 0.05);
	border: 1px solid @kb_line;
	border-radius: 10px;
	padding: 4px;
}

/* --- Tabs -------------------------------------------------------------- */

/* The tabbed area is one sheet with a lid, not four floating panels: a
   short tab used to end in a hole of window background the size of the
   pane, and a long one filled it, so the same control looked like it
   belonged to a different window depending on which tab you were on. */
notebook {
	background-color: @kb_sheet;
	border: 1px solid @kb_line;
	border-radius: 10px;
	margin: 8px;
	padding: 0;
	box-shadow: none;
}
/* The room under the last card matches the room between two of them, so a
   stack of cards ends where it looks like it ends. */
notebook > stack {
	background-color: transparent;
	border: none;
	box-shadow: none;
	padding: 0 0 8px 0;
}
/* One hairline under the row, and the selected tab hangs its accent off
   it. box-shadow has to be named: a themed notebook draws its own rule
   there as a shadow, which would otherwise sit under this one. */
notebook > header {
	background-color: transparent;
	background-image: none;
	border: none;
	border-bottom: 1px solid @kb_line;
	box-shadow: none;
	padding: 0 4px;
	margin: 0;
}
notebook > header > tabs > tab {
	background-color: transparent;
	background-image: none;
	border: none;
	border-bottom: 2px solid transparent;
	border-radius: 6px 6px 0 0;
	box-shadow: none;
	text-shadow: none;
	color: @kb_text_dim;
	font-weight: 600;
	/* 9px either side rather than the 12px a button's label gets. The
	   four labels plus 12px each side came to 469px of strip; at 980x620
	   the strip is 452px, so the Device tab — the one the empty-state
	   hint tells you to go to — was pushed behind a scroll arrow for want
	   of seventeen pixels. This buys 32 of them, which is enough for the
	   four to stand in the open at every size the window opens at. A tab
	   is not a button and does not need a button's shoulders. */
	padding: 8px 9px;
	min-height: 0;
	min-width: 0;
	margin: 0;
}
notebook > header > tabs > tab:hover {
	color: @kb_text;
	background-color: alpha(@kb_text, 0.05);
}
/* The one vocabulary for "on": wash, white label, accent bar. */
notebook > header > tabs > tab:checked {
	color: @kb_text_hi;
	background-color: alpha(@kb_accent, 0.12);
	border-bottom-color: @kb_accent;
}
/* Too many tabs for the width: the arrows that appear are the only way
   to reach the rest, so they are drawn like text rather than like the
   theme's near-invisible chevrons. The four tabs fit unaided down to
   980x620 now — see the padding above — so this is the net under a
   window dragged narrower still, or a desktop set to a larger font.

   The arrows hang off `tabs`, not off `header`: GTK3 builds the row as
   notebook > header.top > tabs > arrow.down, so a rule aimed one level
   up matches nothing and the theme keeps the node. `-gtk-icon-source:
   builtin` is the other half — with a themed image in that property the
   colour below is ignored and the chevron stays whatever the theme drew
   it in (black on this one, 1.26:1). `builtin` hands the drawing back to
   GTK, which renders the arrow in the node's own colour and picks its
   direction from the node itself, so it cannot end up pointing the wrong
   way. Disabled is dimmed but still legible: the pair has to read as one
   control with one end spent, not as two unrelated marks. */
notebook > header > tabs > arrow, notebook > header > arrow {
	color: @kb_text_dim;
	-gtk-icon-source: builtin;
	background-color: transparent;
	background-image: none;
	border: none;
	box-shadow: none;
	min-width: 20px;
	min-height: 20px;
}
notebook > header > tabs > arrow:hover, notebook > header > arrow:hover {
	color: @kb_text_hi;
	background-color: alpha(@kb_text, 0.08);
	border-radius: 6px;
}
notebook > header > tabs > arrow:disabled,
notebook > header > arrow:disabled { color: @kb_text_off; opacity: 0.45; }

/* --- Controls ---------------------------------------------------------- */

button, button.color {
	background-image: none;
	background-color: @kb_raised;
	color: @kb_text;
	border: 1px solid @kb_edge;
	border-radius: 6px;
	box-shadow: none;
	text-shadow: none;
	padding: 4px 12px;
	min-height: 24px;
	transition: background-color 120ms ease-out, border-color 120ms ease-out;
}
button:hover {
	background-color: @kb_raised_hi;
	border-color: @kb_text_off;
}
button:active {
	background-color: shade(@kb_raised, 0.82);
	background-image: none;
	color: @kb_text_hi;
}
/* Still a button. The fill drops a rung rather than becoming the card's
   own colour — which is what it used to do, so that every disabled
   button inside a card was the card with a hairline drawn round it. */
button:disabled {
	background-color: @kb_sheet;
	background-image: none;
	color: @kb_text_off;
	border-color: @kb_edge_off;
}
/* A symbolic icon is recoloured to the node's colour; a full-colour one
   keeps whatever the icon theme drew it in, which against this background
   is often a dark grey smudge. Ask for the symbolic variant. */
button image, entry image { -gtk-icon-style: symbolic; }

/* An icon beside a label is recoloured with it, so it cannot end up a
   grey smudge next to white text.

   These rules used to be the whole defence of the undo and redo buttons,
   which had no label at all, and they were not enough. Two things beat
   them. The shape a name resolves to is the icon theme's business, not
   this file's: under the desktop's own theme here
   object-rotate-left-symbolic came back as an arrowhead, a solid diamond
   and three loose dots where the arc should be, which is not an arrow at
   any size. And the colour stated below is not the colour that lands —
   GTK composites a disabled image at half alpha on top of it, so the
   #9aa3b1 written here was #5a6068 on screen, 2.85:1 against the window
   and under the 3:1 a non-text control has to clear. Both are gone now,
   because the window no longer asks an icon to be a button's only
   meaning; every icon left in it stands beside words. */
button.image-button image, button.flat image { color: @kb_text; }
button.image-button:disabled image, button.flat:disabled image {
	color: @kb_text_off;
}

/* The quietest rung: an outline and nothing else. Undo, Redo, the recent
   colour files and the diagnostic on the Device tab are here.

   The outline is not decoration and it is not optional. These were once
   transparent, borderless and iconless at rest, and a borderless button
   with a word in it is a label: "Recent" and the three file names beside
   it were the same object drawn twice, and the only thing that told you
   which of them you could press was moving the pointer over them. A
   control has to be identifiable without being touched — the same
   @kb_edge every other button in this window carries, 4.3:1 against the
   window and 3.6:1 against a card. What still separates this rung from
   the ordinary one is the fill: an ordinary button is raised, this one
   is not. */
button.flat, .kb-quiet {
	background-color: transparent;
	background-image: none;
	border-color: @kb_edge;
	color: @kb_text;
}
button.flat:disabled, .kb-quiet:disabled {
	background-color: transparent;
	border-color: @kb_edge_off;
	color: @kb_text_off;
}
button.flat:hover, .kb-quiet:hover {
	background-color: @kb_raised;
	border-color: @kb_edge;
	color: @kb_text_hi;
}

/* --- Three rungs of emphasis, and only three ---------------------------

     filled accent   the one action this screen is for   .kb-primary
     accent outline  a second, near-equal action         .kb-secondary
     raised grey     everything else                     button

   One filled button per screen, and it is the thing the screen exists to
   do. It is filled, not outlined, because an outline is what a button
   looks like anyway: an earlier version of this sheet gave every
   in-tab primary an outline instead of a fill and the result was a
   window where nothing was ever the loudest thing — the eye swept a wall
   of identical dark rectangles looking for the verb.

   A screen here can hold two commits at once — Send to keyboard sits
   above the tabs and stays there, and the tab under it has its own (Paint
   these keys, Start, Apply effect). That is not two competitions for the
   same job: they are consecutive steps, and Send spends most of its life
   switched off, where it is a tint and an outline rather than a fill. When
   both are lit, both really are things worth doing next, and saying so is
   more honest than muting one of them on principle.

   .kb-secondary is for the rare screen with a genuine pair — an action
   and its near-equal opposite. It is deliberately not applied to anything
   automatically; a panel that wants the middle rung asks for it. */

/* The one thing a screen is for. Filled in the same blue the board uses
   for a key whose colour has not been sent yet, because pressing Send is
   what clears them.

   The stock dialogs come with us: GTK marks a dialog's default response
   .default, and in a colour chooser or a file chooser that is the button
   that finishes the job — it was arriving in the same grey as Cancel.
   A message dialog is deliberately left out. This window's message
   dialogs default to the cautious answer ("Cancel", "Stop it"), and
   painting the accent onto the answer that throws work away is the exact
   mistake this rung exists to prevent. */
.kb-primary, button.suggested-action, dialog button.default {
	background-color: @kb_accent;
	background-image: none;
	color: @kb_on_accent;
	border-color: @kb_accent;
	font-weight: 700;
}
.kb-primary:hover, button.suggested-action:hover, dialog button.default:hover {
	background-color: shade(@kb_accent, 1.12);
	border-color: shade(@kb_accent, 1.12);
	color: @kb_on_accent;
}
.kb-primary:active, button.suggested-action:active,
dialog button.default:active {
	background-color: shade(@kb_accent, 0.85);
	border-color: shade(@kb_accent, 0.85);
	color: @kb_on_accent;
}
/* Still recognisably the same button when there is nothing to send — the
   width, the weight and the place it sits in do not move — but the accent
   goes. It used to survive as a full outline and a tint, and that turned
   out to be a claim this rung cannot afford to make: the middle rung is
   an accent outline over a faint accent wash, so a switched-off Send and
   a live Paint these keys were the same picture. The accent now means one
   thing, which is that pressing this will do something. */
.kb-primary:disabled, button.suggested-action:disabled,
dialog button.default:disabled {
	background-color: alpha(@kb_accent, 0.05);
	background-image: none;
	color: @kb_text_off;
	border-color: @kb_edge_off;
	font-weight: 700;
}

/* The middle rung: the accent as an outline and a label rather than as a
   fill. Clearly related to the filled button, clearly not it. The wash is
   kept to 8% so this cannot be read as the "on" state — that one is a
   heavier wash with a white label and a bar under it, and a button may not
   mean two things. #66ccff on the washed card is 7.3:1; the outline is the
   same against the card at 8.3:1. */
.kb-secondary {
	background-color: alpha(@kb_accent, 0.08);
	background-image: none;
	color: @kb_accent;
	border-color: @kb_accent;
	font-weight: 700;
}
.kb-secondary:hover {
	background-color: alpha(@kb_accent, 0.20);
	border-color: @kb_accent;
	color: @kb_text_hi;
}
.kb-secondary:active {
	background-color: alpha(@kb_accent, 0.30);
	border-color: @kb_accent;
	color: @kb_text_hi;
}
.kb-secondary:disabled {
	background-color: transparent;
	background-image: none;
	color: @kb_text_off;
	border-color: alpha(@kb_accent, 0.35);
	font-weight: 700;
}

/* Outlined, not filled: an action that throws work away should be easy to
   find and hard to hit by momentum. */
.kb-danger, button.destructive-action {
	background-color: transparent;
	background-image: none;
	color: @kb_danger;
	border-color: @kb_danger_dk;
}
.kb-danger:hover, button.destructive-action:hover {
	background-color: alpha(@kb_danger, 0.16);
	border-color: @kb_danger;
	color: @kb_danger;
}
.kb-danger:active, button.destructive-action:active {
	background-color: alpha(@kb_danger, 0.26);
	border-color: @kb_danger;
	color: @kb_text_hi;
}
.kb-danger:disabled, button.destructive-action:disabled {
	background-color: transparent;
	color: @kb_text_off;
	border-color: @kb_edge_off;
}

/* "This one is on", in the same three signals the selected tab uses: a
   wash of the accent, the label at full white, and a 2px accent bar along
   the bottom edge. The bar is what a colour-blind reader has instead of
   the wash, and it is drawn inside the border so a linked group's own
   outline stays unbroken. */
button:checked {
	background-color: alpha(@kb_accent, 0.18);
	background-image: none;
	color: @kb_text_hi;
	box-shadow: inset 0 -2px 0 0 @kb_accent;
}
button:checked:hover {
	background-color: alpha(@kb_accent, 0.26);
	border-color: @kb_edge;
}
button:checked:active { background-color: alpha(@kb_accent, 0.30); }
/* A group that is switched off still has to show which of it was on. */
button:checked:disabled {
	background-color: alpha(@kb_accent, 0.07);
	color: @kb_text_off;
	border-color: @kb_edge_off;
	box-shadow: inset 0 -2px 0 0 alpha(@kb_accent, 0.45);
}

/* A row of buttons that reads as one control. */
.linked > button {
	border-radius: 0;
	border-right-width: 0;
	margin: 0;
}
.linked > button:first-child {
	border-top-left-radius: 6px;
	border-bottom-left-radius: 6px;
}
.linked > button:last-child {
	border-right-width: 1px;
	border-top-right-radius: 6px;
	border-bottom-right-radius: 6px;
}

/* The swatch is the content, so the chrome around it shrinks to a frame
   of two hairlines: @kb_edge outside, and a dark one just inside it.
   Every colour a keyboard can show then has an edge on some side of it —
   without the light one, black is a hole cut in the card; without the
   dark one, white runs into the card. The colour GTK draws for a
   GtkColorButton is a `colorswatch` node inside that frame, and takes a
   pale inner hairline for the same reason from the other direction. */
.kb-swatch, button.color {
	padding: 4px;
	min-height: 26px;
	min-width: 32px;
	border: 1px solid @kb_edge;
	border-radius: 6px;
	box-shadow: inset 0 0 0 1px alpha(#000000, 0.45);
}
.kb-swatch:hover, button.color:hover { border-color: @kb_text; }
button.color colorswatch, colorswatch {
	border-radius: 4px;
	box-shadow: inset 0 0 0 1px alpha(#ffffff, 0.22);
}
colorswatch.dark overlay, colorswatch.light overlay { border: none; }
/* The one tile in the stock colour chooser that is not a colour: the
   "+" that opens the custom editor. It has no colour of its own to
   draw, so it falls through to the theme and was arriving as a cream
   chip in an otherwise dark dialog. It is a button, so it is dressed as
   one. */
colorswatch#add-color-button, #add-color-button {
	background-color: @kb_raised;
	background-image: none;
	color: @kb_text;
	border: 1px solid @kb_edge;
	box-shadow: none;
}
colorswatch#add-color-button:hover, #add-color-button:hover {
	background-color: @kb_raised_hi;
	border-color: @kb_text;
}
colorswatch#add-color-button overlay { border: none; }

entry, .kb-field, spinbutton, spinbutton > text {
	background-image: none;
	background-color: @kb_raised;
	color: @kb_text;
	border: 1px solid @kb_edge;
	border-radius: 6px;
	box-shadow: none;
	text-shadow: none;
	caret-color: @kb_accent;
	padding: 4px 8px;
	min-height: 24px;
}
entry:disabled, spinbutton:disabled {
	background-color: @kb_surface;
	color: @kb_text_off;
	border-color: @kb_edge_off;
}
entry > placeholder, entry placeholder { color: @kb_text_off; }

combobox > box > button, combobox button.combo {
	background-image: none;
	background-color: @kb_raised;
	color: @kb_text;
	border: 1px solid @kb_edge;
	border-radius: 6px;
	padding: 4px 8px;
	min-height: 24px;
}
/* A combo that cannot be changed has to go quiet the same way a button
   does — same fill, same hairline — or "you cannot pick this yet" is
   spelled two ways in one window. */
combobox > box > button:disabled, combobox button.combo:disabled,
combobox:disabled > box > button {
	background-color: @kb_surface;
	background-image: none;
	color: @kb_text_off;
	border-color: @kb_edge_off;
}
combobox arrow, spinbutton button {
	color: @kb_text_dim;
	background-image: none;
	background-color: transparent;
	border: none;
	box-shadow: none;
}
combobox:disabled arrow, spinbutton:disabled button { color: @kb_text_off; }

checkbutton, radiobutton { color: @kb_text; }
/* -gtk-icon-source: none is the point of this rule. A themed check is a
   bitmap drawn over whatever background is set behind it, so without it
   the box stays the theme's — a white tile with a green tick — however
   dark the box under it is. With no image GTK draws the mark itself, in
   the node's own colour. */
check, radio {
	-gtk-icon-source: none;
	background-image: none;
	background-color: @kb_raised;
	color: @kb_text_hi;
	border: 1px solid @kb_edge;
	box-shadow: none;
	min-width: 16px;
	min-height: 16px;
	border-radius: 4px;
}
radio { border-radius: 50%; }
check:hover, radio:hover { background-color: @kb_raised_hi; }
check:checked, radio:checked, check:indeterminate {
	background-color: @kb_accent;
	border-color: @kb_accent;
	color: @kb_on_accent;
}
check:checked, radio:checked {
	-gtk-icon-source: -gtk-icontheme("object-select-symbolic");
}
check:indeterminate {
	-gtk-icon-source: -gtk-icontheme("list-remove-symbolic");
}
check:disabled, radio:disabled {
	background-color: @kb_surface;
	border-color: @kb_edge_off;
	color: @kb_text_off;
}

scale trough {
	background-image: none;
	background-color: @kb_sunken;
	border: 1px solid @kb_line;
	border-radius: 4px;
	min-height: 8px;
	min-width: 8px;
	margin: 8px 0;
}
scale highlight {
	background-image: none;
	background-color: @kb_accent;
	border-radius: 4px;
}
scale slider {
	background-image: none;
	background-color: @kb_text_hi;
	border: 1px solid @kb_edge;
	border-radius: 50%;
	min-width: 16px;
	min-height: 16px;
	margin: -4px;
}
scale slider:hover { background-color: @kb_accent; }
scale slider:disabled { background-color: @kb_text_off; }
scale > value, scale value { color: @kb_text_dim; font-size: 0.86em; }

progressbar > trough {
	background-color: @kb_sunken;
	border: 1px solid @kb_line;
	border-radius: 4px;
}
progressbar > trough > progress {
	background-image: none;
	background-color: @kb_accent;
	border-radius: 4px;
}

/* The sound meter. Left to the theme it drew twenty near-black cells on a
   near-black card — a control you cannot see is a control that is broken,
   and this one is the only proof the microphone is being heard at all. An
   empty cell is now a visible notch (#2e3035, 1.4:1 on the trough: enough
   to find, not enough to read as lit) and a lit one is the accent at
   10.4:1, so the thing the meter is actually saying is the loud part. */
levelbar trough {
	background-color: @kb_sunken;
	background-image: none;
	border: 1px solid @kb_line;
	border-radius: 4px;
	padding: 2px;
}
/* Small enough that the meter still fits the height the panel asks for,
   big enough that one cell is a cell and not a scratch. */
levelbar block {
	border: none;
	border-radius: 2px;
	min-height: 8px;
	min-width: 8px;
}
levelbar block.empty { background-color: alpha(@kb_text, 0.14); }
levelbar block.filled, levelbar block.low, levelbar block.high,
levelbar block.full { background-color: @kb_accent; }
levelbar:disabled block.filled, levelbar:disabled block.low,
levelbar:disabled block.high, levelbar:disabled block.full {
	background-color: @kb_text_off;
}

expander, expander title, expander title > label { color: @kb_text; }
expander title { padding: 4px 0; }
expander arrow { color: @kb_text_dim; }

separator {
	background-image: none;
	background-color: @kb_line;
	min-width: 1px;
	min-height: 1px;
}

/* A scrolled panel here is not an overlay: the bar is on screen from the
   moment there is anything below the fold, because it is the only thing
   that distinguishes a card the window is too short for from a card that
   is broken. That makes how it looks matter, and it was wrong — the
   slider was a pale blue capsule with grip notches, which is the desktop
   theme's and not this window's. background-color alone could not
   dislodge it: a theme that paints its slider with a background-image
   draws that image over any colour set under it, so the image has to be
   turned off by name as well.

   The steppers go for the same reason. GTK only draws them when the
   theme asks for them, and a theme that does gets two rounded squares
   which this sheet's own button rules then dress exactly like the
   buttons in the cards beside them — a pair of push buttons at the ends
   of a scrollbar, saying nothing about what they do. */
scrollbar {
	background-color: transparent;
	background-image: none;
	border: none;
	-GtkScrollbar-has-backward-stepper: false;
	-GtkScrollbar-has-forward-stepper: false;
	-GtkScrollbar-has-secondary-backward-stepper: false;
	-GtkScrollbar-has-secondary-forward-stepper: false;
}
scrollbar button {
	min-width: 0;
	min-height: 0;
	padding: 0;
	margin: 0;
	border: none;
	background-color: transparent;
	background-image: none;
	box-shadow: none;
	opacity: 0;
}
/* The track is drawn, faintly. A slider floating in the panel colour
   leaves you guessing how far down the panel you are; a track says how
   much is left without asking you to hover anything. */
scrollbar trough {
	background-color: @kb_sunken;
	background-image: none;
	border: none;
	border-radius: 8px;
	margin: 3px;
}
scrollbar slider {
	background-color: alpha(@kb_text, 0.34);
	background-image: none;
	border: 3px solid transparent;
	box-shadow: none;
	background-clip: padding-box;
	border-radius: 8px;
	min-width: 8px;
	min-height: 24px;
}
scrollbar.horizontal slider { min-width: 24px; min-height: 8px; }
scrollbar slider:hover { background-color: alpha(@kb_text, 0.52); }
scrollbar slider:active { background-color: @kb_accent; }

/* The mark a scrolled area draws on the side that has more content behind
   it. Worth keeping — it is the only sign that a panel is taller than the
   window — but GTK turns it on for any overflow at all, and at the
   default size the Colors tab overflows by a few pixels of card margin.
   A dashed rule does not scale with that: with nothing meaningful below
   it, it read as a border struck through the bottom of the last card.
   A fade does. Two pixels of overflow is a breath of shade on the edge
   and easy to ignore; a screenful is a clear soft edge that says the
   content runs on. Tinted from the window colour, so it darkens whatever
   it lies over instead of drawing a line of its own. */
undershoot.top, undershoot.bottom {
	background-color: transparent;
	background-repeat: no-repeat;
	background-size: 100% 16px;
}
undershoot.top {
	background-image: linear-gradient(to bottom, alpha(@kb_root, 0.85),
		alpha(@kb_root, 0));
	background-position: center top;
}
undershoot.bottom {
	background-image: linear-gradient(to top, alpha(@kb_root, 0.85),
		alpha(@kb_root, 0));
	background-position: center bottom;
}
undershoot.left, undershoot.right {
	background-color: transparent;
	background-repeat: no-repeat;
	background-size: 16px 100%;
}
undershoot.left {
	background-image: linear-gradient(to right, alpha(@kb_root, 0.85),
		alpha(@kb_root, 0));
	background-position: left center;
}
undershoot.right {
	background-image: linear-gradient(to left, alpha(@kb_root, 0.85),
		alpha(@kb_root, 0));
	background-position: right center;
}

/* --- Menus, status, messages ------------------------------------------- */

menubar {
	background-image: none;
	background-color: @kb_root;
	color: @kb_text;
	border: none;
	padding: 0 4px;
}
menubar > menuitem {
	color: @kb_text;
	padding: 6px 12px;
	border-radius: 6px;
}
menubar > menuitem:hover, menubar > menuitem:selected {
	background-color: @kb_raised;
	color: @kb_text_hi;
}
menu, .menu {
	background-image: none;
	background-color: @kb_raised;
	color: @kb_text;
	border: 1px solid @kb_edge;
	padding: 4px 0;
}
/* background-color has to be stated even though the menu behind it is
   already painted. A menu item is not a run of text on the menu's
   surface: it is a node with a background of its own, and a theme that
   fills it opaque white — Luna does — keeps that fill under our pale
   label whatever the menu under it is set to. Every row of the File and
   Help menus went to 1.19:1 for want of this one line. The label node is
   named for the same reason: several themes colour it directly, which
   beats inheriting from the item. */
menu menuitem, menu menuitem label, menu > menuitem > label {
	background-color: transparent;
	background-image: none;
	color: @kb_text;
	text-shadow: none;
}
menu menuitem { padding: 4px 12px; }
menu menuitem:hover, menu menuitem:selected,
menu menuitem:hover label, menu menuitem:selected label {
	background-color: @kb_accent;
	background-image: none;
	color: @kb_on_accent;
}
menu menuitem:disabled, menu menuitem:disabled label { color: @kb_text_off; }
/* The submenu chevron on Recent — the only submenu in the window, and
   the only thing that says Recent opens one rather than doing something.

   It asked for `builtin` here, on the theory that GTK would then draw the
   shape itself in the node's own colour rather than blitting a bitmap
   from the theme. It does not: this node is rendered through
   gtk_css_style_render_icon(), which draws a builtin image as
   GTK_CSS_IMAGE_BUILTIN_NONE — nothing at all. Measured on a screenshot
   of the open File menu, the row read "Recent" with bare menu to the
   right of it, which is a control with its one affordance missing.

   A symbolic icon is recoloured to the node's colour, so naming the
   theme's chevron costs nothing that `builtin` was buying, and the sheet
   forces an icon theme that has it. */
menu menuitem arrow, menu menuitem > arrow {
	color: @kb_text_dim;
	-gtk-icon-source: -gtk-icontheme("pan-end-symbolic");
	-gtk-icon-style: symbolic;
	min-width: 16px;
	min-height: 16px;
	background-color: transparent;
	background-image: none;
	border: none;
}
menu menuitem:hover arrow { color: @kb_on_accent; }
menu separator { margin: 4px 0; }

/* The one place that says what just happened, so it is a sentence at the
   size sentences are set at here, made secondary by colour alone — the
   same treatment .kb-subtitle gets, because it is the same kind of line.
   Shrinking it was the wrong economy: this is the text a person looks at
   to find out whether the thing they just did worked. The rule above it
   is the same weight as the one between the two halves of the window,
   because it divides the same kind of thing — a region from a region,
   rather than a card from the sheet it lies on. */
statusbar, .kb-status {
	background-color: @kb_root;
	border-top: 1px solid alpha(@kb_edge, 0.55);
	color: @kb_text_dim;
	font-size: 1em;
	padding: 8px 12px;
}
statusbar label { color: @kb_text_dim; font-size: 1em; }

/* A failure stays on screen until it is dismissed, so it is tinted rather
   than shouted: red enough to find, quiet enough to live with. */
infobar {
	background-image: none;
	background-color: @kb_surface;
	border: 1px solid @kb_line;
	border-radius: 10px;
	margin: 8px 8px 0 8px;
}
infobar.error, infobar.warning {
	background-color: #3a1f1c;
	border-color: @kb_danger_dk;
}
/* The bar's own node carries the tint; the box inside it carries the
   padding. Both drawing a border is where the doubled outline came from. */
infobar > revealer > box {
	background-color: transparent;
	background-image: none;
	border: none;
	padding: 8px 12px;
}
infobar label { color: @kb_text; }
infobar.error label, infobar.warning label { color: #ffd7d0; }

tooltip, tooltip.background {
	background-color: @kb_raised;
	border: 1px solid @kb_edge;
	border-radius: 6px;
}
tooltip label { color: @kb_text; padding: 2px; }

/* --- Focus ------------------------------------------------------------- */

/* Focus is stated twice over, and the second half is the important one.

   `outline` is what GTK renders — but only for widgets whose draw handler
   calls gtk_render_focus, and only while the toplevel is the active
   window. A check box, a radio, the button inside a combo, a slider and
   an expander's title never call it, so on this sheet those five used to
   take keyboard focus with nothing at all to show for it: tabbing through
   the window, the caret simply vanished for five stops and came back.
   Every rule below therefore also draws a ring of its own with
   box-shadow, on a node that is painted unconditionally.

   The mark itself is the same on everything: the control's own 1px edge
   turned accent, and a 2px ring immediately inside it — one solid 3px
   band, not a hairline you have to hunt for, and the same 3px whether
   the control is a 34px button or a 16px check box. 8.3:1 against a
   card, 7.2:1 against a control's own fill; on the accent-filled primary,
   where an accent ring would vanish, the same band in @kb_on_accent at
   10.4:1.

   :focus, not :focus-visible — GTK 3.24 has no such selector; the
   pointer-vs-keyboard distinction is made by gtk_widget_has_visible_focus
   for the outline half, and the ring half is worth showing either way. */
* {
	outline-color: @kb_accent;
	outline-style: solid;
	outline-width: 2px;
	/* -1px, so where GTK does draw the outline it lands exactly on the
	   ring below and the two are one 3px mark. At -3px they stacked into
	   a 5px band on the widgets that have both and stayed 3px on the
	   ones that do not, which is a focus ring that changes width as you
	   tab along the row. */
	outline-offset: -1px;
	-gtk-outline-radius: 6px;
}
button:focus, entry:focus, spinbutton:focus, .kb-swatch:focus,
button.color:focus, combobox:focus > box > button, combobox:focus button.combo,
checkbutton:focus > check, radiobutton:focus > radio,
check:focus, radio:focus {
	border-color: @kb_accent;
}
/* The ring the sheet draws itself. Inset, so it cannot be clipped away by
   a parent that allocates the widget exactly. */
button:focus, entry:focus, spinbutton:focus, .kb-swatch:focus,
button.color:focus, combobox:focus > box > button, combobox:focus button.combo {
	box-shadow: inset 0 0 0 2px @kb_accent;
}
/* A tab is the one control here whose whole border is a single 2px line
   along the bottom, and that line is what "this is the tab you are on"
   means. Turning it accent for focus — which is what a blanket
   border-color rule did — made every tab you merely tabbed past look
   like the tab you had arrived at. The ring only, then. */
notebook > header > tabs > tab:focus {
	box-shadow: inset 0 0 0 2px @kb_accent;
}
/* A check box is 16px square, which leaves no room inside it for a ring.
   The ring goes round the whole control instead — box and label together,
   which is also the whole of what you can click. Drawn as a shadow and
   not as a wash on purpose: a wash behind a label is this sheet's word
   for "switched on", and a box that is merely focused must not borrow it. */
checkbutton:focus, radiobutton:focus {
	box-shadow: inset 0 0 0 2px @kb_accent;
	border-radius: 6px;
}
checkbutton:focus > check, radiobutton:focus > radio,
check:focus, radio:focus {
	border-color: @kb_accent;
}
/* A slider is a circle with nothing inside it either, and half its travel
   is over the accent-filled part of the trough, where an accent ring on
   its own would simply merge into the fill. So the ring is a pair: accent
   against the white handle, then dark against whatever the trough is
   doing there. The wider shadow is written first because GTK3 paints a
   shadow list back to front — the reverse of what the CSS spec says, and
   the reason the first attempt at this drew one flat 4px halo. The trough
   lights up as well: at 980px wide the handle is a small thing to find. */
scale:focus > trough > slider, scale:focus slider {
	box-shadow: 0 0 0 4px @kb_sunken, 0 0 0 2px @kb_accent;
	border-color: @kb_accent;
}
scale:focus > trough, scale:focus trough { border-color: @kb_accent; }
/* An expander's hit target is its whole title row. */
expander:focus > title, expander:focus title {
	box-shadow: inset 0 0 0 2px @kb_accent;
	border-radius: 6px;
}
/* An accent ring is invisible on an accent fill, so the primary button
   takes the dark end of the same pair. 10.4:1 on the fill. */
.kb-primary:focus, button.suggested-action:focus, dialog button.default:focus {
	outline-color: @kb_on_accent;
	border-color: @kb_on_accent;
	box-shadow: inset 0 0 0 2px @kb_on_accent;
}
/* ...and back again for the middle rung, whose fill is dark: a dark ring
   on a dark wash is no ring at all. */
.kb-secondary:focus {
	outline-color: @kb_accent;
	border-color: @kb_accent;
	box-shadow: inset 0 0 0 2px @kb_accent;
}
/* A swatch keeps the dark hairline that stops white running into the
   card; the ring goes outside it, against the swatch's own frame. Widest
   shadow first again — written the other way round the translucent black
   lay over the accent and the ring came out #38708c, a muted blue that
   was neither the ring nor the hairline. Stated after the two rules above
   so it survives whichever of them also matched. */
.kb-swatch:focus, button.color:focus {
	box-shadow: inset 0 0 0 3px alpha(#000000, 0.45), inset 0 0 0 2px @kb_accent;
}

/* --- The board --------------------------------------------------------- */

/* The stage. The board used to sit in a rectangle of near-black on a
   window of slightly-less-near-black, with no border, no rounding and no
   separator — which read as a hole cut in the window rather than as
   somewhere a keyboard is being shown. The two fills are still 1.04:1
   apart and cannot be anything else: the floor has to be exactly the
   colour KeyboardScene clears its viewport to, or the rendered board
   stands on a visible seam. So the stage is drawn by its lip instead — a
   rounded rim at 4.3:1 against the window and 4.5:1 against its own
   floor, with a shadow cast inwards from it so the surface has depth.

   The second selector is structural because the widget it dresses has no
   name of its own: the board is the only thing in the left half of the
   pane that scrolls. Anything that rearranges that pane should add
   `.kb-stage` to whatever ends up holding the board and drop the
   structural half of this rule. */
.kb-stage,
paned > box > scrolledwindow {
	background-image: none;
	background-color: @kb_sunken;
	border: 1px solid @kb_edge;
	border-radius: 14px;
	box-shadow: inset 0 2px 10px alpha(#000000, 0.65);
	margin: 8px;
	padding: 10px;
}
.kb-stage > border { border-style: none; }
/* The viewport inside it is scenery, not a second frame: GtkScrolledWindow
   marks it .frame, and every other viewport in the window as well, so the
   theme's outline has to go for all of them. */
viewport, viewport.frame { border: none; box-shadow: none; }

/* Near black, so a lit key is the brightest thing in the window and the
   colour of one is not read against a neighbouring grey. This is the
   colour KeyboardScene clears its viewport to, so the flat board and the
   3D one are shown on the same surface. The class is on a GtkOverlay,
   which draws no background of its own; the event box directly inside it
   is the node that paints. */
.kb-board, .kb-board > widget { background-color: @kb_sunken; }

/* Keycap colours are the LED colours themselves and come from per-widget
   providers (styling::paint), which also pick a legend colour with enough
   contrast against them. What is left here is the unlit cap.

   The rings that carry hover, selection and pending state are NOT here,
   and the three classes below are state, not appearance: they are what
   KeyCap::on_draw reads to decide which bands to draw. A ring stated as
   a border-color is a constant, and a constant cannot be seen on a key
   painted that constant — paint every key #f5c211 and a `border-color:
   @kb_amber` selection is gone. The sheet cannot fix that, because a
   rule here does not know what colour the key underneath is; the cap
   does, so it draws its own marks through styling::markColor, which is
   the same function the model's shader marks by.

   The 2px transparent border stays: it is the room the marks are drawn
   in, and it is what keeps the legend off the edge of the cap whether
   the key is marked or not. */
.kb-key {
	font-size: 8pt;
	padding: 2px 0px;
	border-radius: 4px;
	border: 2px solid transparent;
	background-color: @kb_cap;
	color: @kb_text;
	min-width: 36px;
	min-height: 36px;
}
.kb-led { font-size: 7pt; background-color: @kb_cap_led; }
.kb-key:disabled { opacity: 0.4; background-color: @kb_cap_off; }
)CSS";
	}

	// The icons are part of the sheet, and CSS cannot reach them.
	//
	// Everything this window draws it draws itself, in one palette, at
	// one weight — except the handful of stock icons it asks the
	// desktop's icon theme for, which arrive in whatever house style that
	// theme happens to have. On this machine the theme is Oxygen, whose
	// list-remove-symbolic is a cross: under a check box that means "no"
	// rather than the "some of them" it is there to say. Its
	// object-rotate-left-symbolic was an arrowhead, a solid diamond and
	// three loose dots, which is why the two buttons that used to ask for
	// it now say "Undo" and "Redo" in words instead.
	//
	// CSS cannot fix a wrong shape. -gtk-icon-source is consulted for
	// nodes GTK draws itself (an arrow, a check mark); a GtkImage renders
	// the surface it was handed and ignores the property — measured, not
	// assumed. So the choice is the theme itself. Adwaita ships with GTK,
	// carries every name this window uses, and is drawn for exactly this
	// job: one weight, one colour, recoloured by the node it sits in.
	//
	// Guarded, because a build with no Adwaita installed must not end up
	// with no icons at all: the swap only happens if a probe theme can
	// actually find them. The three names probed are three the window
	// really asks for — the eyedropper on the colour card, the mark on
	// the notice bar, and the tick GTK puts beside a chosen menu item.
	inline void useMatchingIconTheme(const Glib::RefPtr<Gtk::Settings> &settings) {
		Glib::RefPtr<Gtk::IconTheme> probe = Gtk::IconTheme::create();
		probe->set_screen(Gdk::Screen::get_default());
		probe->set_custom_theme("Adwaita");
		if (probe->has_icon("color-select-symbolic") &&
		    probe->has_icon("dialog-warning-symbolic") &&
		    probe->has_icon("object-select-symbolic"))
			settings->property_gtk_icon_theme_name() = "Adwaita";
	}

	// Puts the sheet above the desktop theme, and asks GTK for the dark
	// variant of whatever that theme is. The second part is a courtesy:
	// a theme with no dark variant ignores it, which is why the sheet
	// states its own colours rather than shading the theme's.
	inline void installApplicationStyle() {
		if (Glib::RefPtr<Gtk::Settings> settings = Gtk::Settings::get_default()) {
			settings->property_gtk_application_prefer_dark_theme() = true;
			useMatchingIconTheme(settings);
		}
		Glib::RefPtr<Gtk::CssProvider> provider = Gtk::CssProvider::create();
		try {
			provider->load_from_data(applicationCss());
		} catch (const Glib::Error &error) {
			std::fprintf(stderr, "g810-led-gui: failed to load CSS: %s\n",
				error.what().c_str());
			return;
		}
		Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(),
			provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

}

#endif
