# GUI overhaul — checkpoint (paused)

**Tree healthy.** `make gui` clean, all seven harnesses green, `shots`
captures 24 screens cleanly. Watch page: `GUI_PROGRESS.html`.

**Score: 6.5/10**, third whole-window critic, not wowed. Trajectory:
3 and 4 → 5, 5 → **6.5**.

---

## What the critic verified as good

> "Every act I drove through `flows` reported itself in the foot in plain
> words with the right numbers — 'Key G is already #ff0000', 'Painted 3 keys
> #ff0000 — the rest already were', 'Raindrop stopped — your colors are
> back'. Contrast passes everywhere I sampled (body ink 7.0:1 to 13.1:1;
> Send/Discard borders 3.28:1, group chips 3.57:1). Every control carries a
> real ATK name and most a written description; the board is one Tab stop
> named 'Keyboard', not a hundred and eight. **Keyboard-only painting works
> and narrates itself**: arrows say 'On key F2 — #ff0000', Space paints,
> Delete says 'Turned key 5 off'. **The two board views mark a selection by
> the same rule** — I painted the whole board the exact amber the mark is
> drawn in and both views still drew a legible dark-gold ring. **A live
> effect costs nothing**: worst frame 23.4ms against 17.0ms idle. The error
> bar wraps and keeps its close button at 980x620. Six of the eight items
> handed to the previous round were fixed, and fixed well."

## Fixed at the checkpoint

**The board's well was stretched to double the height it asked for.**
`MainWindow.cpp` packed the stage `expand, fill`, throwing away the
aspect-correct height `BoardStage::get_preferred_height_for_width_vfunc`
computes: the flat board asked for 252px at 1500×860 and was handed 537, so
the keyboard floated in 150px of black above and below — the largest region
of the window, 42% full, at every size. Now `expand, no fill`. Verified: the
frame is sized to the board.

## What is still wrong — start here

**Major**
- **The On-board effects tab contradicts itself in the state it opens in.**
  The strip under the tab row is pinned to "Runs on the keyboard once you
  press Apply effect" (`MainWindow.cpp:735`), while with Effect = Off the
  card says "There is nothing to apply", the Apply button is insensitive and
  the foot says "Off — no effect to apply". Three of four were made honest;
  the first line you read points at a dead button.
- **"Showing the colors you last sent" sits above a board showing colors you
  have not sent** (`PanelStrip.cpp:624`). The sentence is about the hardware
  but is positioned as a caption for the on-screen twin, so it reads as the
  opposite of the truth. The on-board preview state gets this right —
  "Previewing Waves — the keyboard is still showing your colors" names both
  subjects.
- **A failure blanks the status line.** After a failed Open the statusbar
  label measures 1×23 — empty. The one line that says what just happened
  goes silent exactly when something went wrong.

**Minor**
- Switching to Live effects shifts every control up 34px: its card heading is
  hidden while the equally redundant neighbour keeps one.
- Recent-file chips are built with no existence check, so a dead path is
  offered as a live button guaranteed to raise the error bar.
- One fact, two wordings, on screen together: "16 keys selected — press Esc
  to let go" vs "Selected 16 keys". And dropping a selection has three names
  — Deselect / let go / drop.
- Accessible *descriptions* are thorough on some panels, absent on others.

**Unverified**
- Tab traversal order. The critic could not test it: this X session never
  gives the window toplevel focus, so `child_focus` and
  `gdk_test_simulate_key` both leave focus parked. The explicit focus chain
  at `MainWindow.cpp:229-234` was read and every control has `can_focus` and
  a name, but the real order has not been observed.

## To resume

1. The three majors above — all have file and line.
2. Then a fourth whole-window critic.
3. `MainWindow.cpp:735`, `PanelStrip.cpp:624` and the statusbar path are the
   whole of the major list; the minors are one panel each.

## Housekeeping

Dead entries in your recent list from the critics' failed-load probes
(`no-such-profile-at-all`, `err-probe` in `~/.config/g810-led/recent`). I did
not edit that file — it is yours.

---

## History

| Wave | Result |
|---|---|
| 0 | Ran the real thing, captured 28 screens, inventoried 15 friction items |
| 1 | Split the 4 640-line monolith into 6 files (verified byte-identical, then pixel-identical against a control build); camera solves containment from the board's corners; dark stylesheet. Fixed two infrastructure lies: `make gui` was a no-op after header edits, and the harness build script reported success for failed builds. Critics: **3/10, 4/10** |
| A | A real use-after-free in the shipped app, found by backtrace: a lambda capturing a child widget, connected to a longer-lived signal, firing through freed memory at teardown. Fixed with `sigc::track_obj` |
| 2 | Eight owners on the measured defects. Cut short twice by session limits; 15 of 26 agents landed |
| 3 | The window shell — the file no agent owned. One vocabulary (send / unsent / discard); `Ctrl+?` had never worked on any layout where `?` is shifted; Tab order was decided by pane width because a GtkPaned gives both halves `x=0`. Two blockers fixed by hand: the window opened at a size its own controls did not fit, and a failed profile load still adopted the file. Critic: **5/10, twice** |
| 4 | Effects honesty, one rule for selection marks across both views, secondary-button contrast, error bar, combo accessible names. Critic: **6.5/10** |

## Infrastructure (reusable)

- `$TMP/shots.cpp` — 24 screens: every tab, every effect, painted board, flat
  board, dialogs, 980×620 and 1900×1000.
- `$TMP/firstrun.cpp` — the window exactly as `main()` opens it, no resize.
  This is what caught the launch-size blocker.
- `$TMP/flows.cpp` — drives the journeys, prints what the window says after
  each step plus the Tab order with accessible names.
- `$TMP/RUBRIC.md` — the standard. `$TMP/baseline/` — 28 screens of "before".
- `$TMP/mkprogress.sh` — regenerates the watch page.
