# g810-led</br>

Linux led controller for Logitech G213, G410, G413, G512, G513, G610, G810, G815, G910 and GPRO Keyboards.</br>

## Compatible keyboards :</br>
- **G213 Prodigy**</br>
- **G410 Atlas Spectrum**</br>
- **G413 Carbon**</br>
- **G512 Carbon**</br>
- **G513 Carbon**</br>
- **G610 Orion Brown**</br>
- **G610 Orion Red**</br>
- **G810 Orion Spectrum**</br>
- **G815 LIGHTSYNC**</br>
- **G910 Orion Spark**</br>
- **G910 Orion Spectrum**</br>
- **GPRO**</br>

## Contribute and evolution :</br>
* [CONTRIBUTING.md](https://github.com/MatMoul/g810-led/blob/master/CONTRIBUTING.md)

## Install :</br>
* [INSTALL.md](https://github.com/MatMoul/g810-led/blob/master/INSTALL.md)

## Profiles :<br>
You can load predefined configurations on startup! 
* [PROFILES.md](https://github.com/MatMoul/g810-led/blob/master/PROFILES.md)

## Help :</br>
`g213-led --help`</br>
`g410-led --help`</br>
`g413-led --help`</br>
`g512-led --help`</br>
`g513-led --help`</br>
`g610-led --help`</br>
`g810-led --help`</br>
`g815-led --help`</br>
`g910-led --help`</br>
`gpro-led --help`</br>

`g810-led --help-keys`</br>
`g810-led --help-effects`</br>
`g810-led --help-samples`</br>

## Samples :</br>
`g810-led -p /etc/g810-led/profile # Load a profile`</br>
`g810-led -k logo ff0000 # Set color of a key`</br>
`g810-led -a 00ff00 # Set color of all keys`</br>
`g810-led -g fkeys ff00ff # Set color of a group of keys`</br>
`g810-led --startup-mode color # Set keyboard power on effect`</br>
`g810-led -fx color keys 00ff00 # Set fixed color effect`</br>
`g810-led -fx breathing logo 00ff00 0a # Set breathing effect`</br>
`g810-led -fx cycle all 0a # Set color cycle effect`</br>
`g810-led -fx hwave keys 0a # Set horizontal wave effect`</br>
`g810-led -fx vwave keys 0a # Set vertical wave effect`</br>
`g810-led -fx cwave keys 0a # Set center wave effect`</br>

## Samples with no commit :</br>
`g810-led -an 000000 # Set color of all key with no action`</br>
`g810-led -gn modifiers ff0000 # Set color of a group with no action`</br>
`g810-led -kn w ff0000 # Set color of a key with no action`</br>
`g810-led -kn a ff0000 # Set color of a key with no action`</br>
`g810-led -kn s ff0000 # Set color of a key with no action`</br>
`g810-led -kn d ff0000 # Set color of a key with no action`</br>
`g810-led -c # Commit all changes`</br>

## Samples for G610 :</br>
`g610-led -a 60 # Set intensity of all keys`</br>
`g610-led -k logo ff # Set intensity of a key`</br>
`g610-led -g fkeys aa # Set intensity of a group of keys`</br>

## Samples for G213 :</br>
`g213-led -a 00ff00 # Set all keys green`</br>
`g213-led -r 1 ff0000 # Set region 1 red`</br>

## Samples with pipe (for effects) :</br>
`g810-led -pp < profilefile # Load a profile`</br>
`echo -e "k w ff0000\nk a ff0000\nk s ff0000\nk d ff0000\nc" | g810-led -pp # Set multiple keys`</br>

## Testing unsupported keyboards :</br>
Start by retrieving the VendorID and the ProductID of your keyboard using lsusb.</br>
`lsusb`</br>
Sample return :<br>
`Bus 001 Device 001: ID 046d:c331 Logitech, Inc.`</br>
In this sample VendorID is 046d and ProductID is c331. Now test your keyboard with all supported protocol (for 2019 keyboard start with -tuk 4):</br>
`g810-led -dv 046d -dp c331 -tuk 1 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (1), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 2 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (2), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 3 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (2), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 4 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (3), if not, need new dump.</br>

## Building and linking against the libg810-led library :</br>
Include in implementing source files.</br>
```cpp
#include <g810-led/Keyboard.h>
```
To link, simply provide `-lg810-led` to the build flags.</br>

To build the g810-led application as a dynamically-linked variant, run the target:</br>
`make bin-linked`</br>

## Dumps :
Dumps of keyboards are now stored in a separate project to preserve a small download size of this project.
You can find them here : [https://github.com/MatMoul/g810-led-resources](https://github.com/MatMoul/g810-led-resources)

## GUI :

`g810-led-gui` is a graphical front end for this library: a picture of your
keyboard that you paint, and the effects the keyboard and this computer can
run. It is an experiment rather than a finished product — it started as
"what would g810-led look like with a window?" and is being improved as
things turn out to be wrong. The command line is unchanged and keeps
working exactly as it did.

### Building and running

You need a C++11 compiler, `gtkmm-3.0`, `libepoxy` and hidapi. Optional, each
enabling one thing:

| package | what you lose without it |
|---|---|
| `libpulse` | sound-reactive lighting |
| `libpipewire-0.3` + `glibmm-2.4` | screen colors |
| `ayatana-appindicator3` (or `appindicator3`) | the tray icon |

```sh
make gui          # builds bin/g810-led-gui
./bin/g810-led-gui
```

Anything missing is compiled out and the window says so where that feature
would have been, rather than failing to build.

Your keyboard has to be writable by your user. This project ships udev rules
that rely on `uaccess`; if the window says the device is closed to you, it
prints the exact rule and the command to install it.

### First run

The window opens on your keyboard, drawn in 3D. Pick a colour, click keys to
paint them, and press **Send to keyboard**. Nothing reaches the hardware until
you do — everything before that is a draft you can undo.

The one habit worth learning: the strip above the tabs is the truth. It says
which keyboard is connected, what the keys are showing *right now*, and
whether you have anything unsent.

### What it does

The window is organised around what you are trying to do. Along the very top is
a **File** menu — open colors, the files you opened recently, paste colors as
text, save, save as, quit — a **Help** menu with the shortcut list, and at the
right end Undo and Redo. A strip above the tabs always says which
keyboard is connected, what the board is showing right now (your colors, an
effect, or a live effect) and how many changes are still unsent. Below it are
the four tabs:

* **Colors** — paint keys, key groups and regions. These are *staged*: nothing
  reaches the keyboard until you press **Send to keyboard** (Ctrl+Enter).
  Editing is undoable (Ctrl+Z / Ctrl+Shift+Z), hovering a key-group chip
  previews that group on the board, and right-clicking a key offers paint,
  clear, pick, and select-row / select-same-color.
* **On-board effects** — the effects the keyboard runs by itself. These apply
  immediately; turning one off puts your colors back.
* **Live effects** — raindrop, raindrop over your colors, wave, sound
  reactive and screen colors, driven by this computer. One runs at a time,
  and stopping it restores your colors.
* **Device** — which keyboard to talk to when more than one is plugged in,
  M/G keys, startup mode, lighting control, and an *Advanced* section for
  unsupported or test keyboards. Underneath is **Everything else in this
  machine**: your memory read from its own SPD, the USB devices from makers
  who ship lighting, and what Bluetooth knows about. That list is read-only
  and says of each thing whether this application can light it — which,
  mostly, it cannot. It is there to answer "is my hardware supported?"
  without guesswork.

Effects and live effects behave as previews: whatever they do to the board,
stopping them brings your scheme back. Ctrl+? lists the shortcuts.

### The board :
The keyboard on the left is a model of the one on your desk: the keys stand
proud of a brushed plate, the legends light up in the colour you give them, and
the light spills onto the plate around each cap the way it does on a G512.

* **drag with the right button** to turn it, **scroll** to zoom, **drag with the
  middle button** to slide it, **Home** to put it back
* right-click opens the key menu, double-click picks a colour, shift-click adds
  one key to the selection

The left button is a tool, and which one it is holding is the **Paint / Select**
toggle above the board:

* **Paint** — click a key to stage the current colour on it, or drag across
  keys to paint the ones you cross (one stroke, one undo). A drag that starts
  on the bare plate still rubber-bands a selection.
* **Select** — click a key to select just that one, or drag to rubber-band a
  group. Nothing is painted; use *Paint selection* when the group is right.

They were one gesture before, which meant a single key could not be selected
without also being painted.

The model is generated from the layout of whichever keyboard is connected, in
real millimetres (one key unit is 19.05 mm), so a TKL, a board with a G-key
column or one with a media cluster each come out as themselves rather than as a
G512 with pieces missing.

Drawing it needs an OpenGL 3.3 context, which comes from GTK and libepoxy — no
extra dependency. Where that is not available the flat grid of keys takes over
by itself and says so; the **3D board** checkbox under the keyboard switches
between the two at any time, and `G810_FORCE_2D=1` starts in the flat one.

### The tray icon :
Closing the window while a live effect is running does not stop it. The window
hides, the effect keeps running in the same process, and an icon appears in the
system tray whose menu carries the status and the controls worth having without
the whole window:

```
Board: screen colors · capturing screen (2560×1440)
G512 RGB MECHANICAL GAMING KEYBOARD
──────────
Show window
Stop effect
Live effect  ▸   Raindrop · Raindrop over your colors · Wave · Sound reactive · Screen colors
Mode         ▸   the modes of whichever effect is selected
──────────
Quit
```

The status line is the same sentence the strip at the top of the window shows,
and the two submenus are built from the window's own controls, so the tray can
never offer a different set of choices. **Quit** stops the effect and puts your
colors back before exiting — anything you want left running belongs in the tray,
not behind a closed program.

The icon uses StatusNotifierItem (through libayatana-appindicator), which is
what KDE, GNOME and most panels speak, and is the only tray protocol that works
under Wayland. Without an appindicator library at build time, or on a desktop
with no status-notifier host, the GUI checks for one at run time and falls back
to what it did before: raindrop and sound reactive are handed to a detached
background process, and screen colors asks whether to do the same.

### Sound reactive lighting :
The **Live effects** tab lights the keyboard from live audio on models with
per-key RGB. Choose *Sound reactive*, pick what to listen to — *Default output
(what you hear)* for music and games, *Default input* for the microphone, or
any source the sound server reports — then press **Start**. Modes:

* **Spectrum bars** — columns are frequency bands, the lit height of a column is that band's level
* **Rainbow spectrum** — a fixed hue per column, brightness follows the band
* **Level meter** — the whole board tracks loudness
* **Beat flash** — a flash on every detected beat

*Quiet*/*Loud* set the color gradient. Under *Fine tuning*, *Sensitivity*
scales the input and *Smoothing* controls how slowly a bar falls back after a
peak; *Auto gain* normalizes against the loudest recent peak, so quiet material
still fills the board — turn it off to drive the level from *Sensitivity*
alone. Mode, colors, sensitivity and smoothing can all be changed while it
runs.

**Blend over the current colors** (on by default) mixes the effect into the
key colors from the Colors tab instead of over black, so silence leaves the
board on your own scheme and the legends stay readable — the louder a key's
band, the more of the effect color it takes. Turn it off for the classic
black-background visualizer.

Closing the window leaves it running in the tray, where the menu shows what it
is listening to and **Stop effect** ends it. On a desktop with no tray the
effect is handed to a detached background process (`g810-led-gui
--audio-daemon`) instead, the same handoff the raindrop animation uses; open the
GUI again and it picks the running effect back up, with the controls showing its
settings, and **Stop** ends it and restores your colors.

Saving a profile records the effect as an `audio` line:

```
audio bars 0066ff ff0066 100 45 auto overlay alsa_output.hdmi-stereo.monitor
#     mode quiet  loud   gain smooth auto|fixed  overlay|solid  source (optional)
```

The key colors the effect blends over are the `a`/`k` lines of the same
profile, so a profile carries the scheme and the visualizer together.

Loading that profile — including the automatic reload of the last profile at
startup — turns sound-reactive lighting back on. `audio off` records that it
was not running. The CLI ignores the line, so such a profile still works with
`g810-led -p`. A source that no longer exists falls back to the default
output.

Audio capture needs libpulse (PulseAudio, or PipeWire through its PulseAudio
server) at build time — on Debian/Ubuntu `libpulse-dev`, on Arch `libpulse`,
on Fedora `pulseaudio-libs-devel`. Without it the GUI still builds and the
Audio tab reports that the feature was compiled out.

### Screen colors :
The **Live effects** tab can also paint the keyboard from what is on screen.
Choose *Screen colors* and press **Start**; your desktop asks which screen or
window to share, and remembers the answer, so later starts go straight to the
lights. *Choose a different screen…* forgets that answer and asks again.
Modes:

* **Mirror the screen** — each key takes the color of the part of the screen
  above it, so the board becomes a very low resolution copy of the display
* **Single dominant color** — one color for the whole board, weighted towards
  the colorful parts of the screen rather than its flat average, which on a
  normal desktop is a muddy grey

*Color boost* pushes what the keys show towards saturated color — screens are
mostly muted greys, and a keyboard reproducing that looks broken; 0 reproduces
the screen exactly. *Smoothing* controls how slowly a key follows the screen,
which is what stops video from strobing the board. **Blend over the current
colors** mixes the screen into the colors from the Colors tab instead of over
black, so the dark parts of the screen leave your own scheme showing. All of
these can be changed while it runs.

The state strip says `Board: screen colors · capturing …` in bold for as long
as it runs. Closing the window keeps it running and puts it in the tray, where
the icon and its status line say the screen is being read and **Stop effect**
is one click away — that visibility is the point, since this is the one effect
that reads your display.

On a desktop with no tray, closing **asks** instead: *Stop it* (the default)
ends the capture and puts your colors back, *Keep running* hands it to a
detached background process. Reopen the window then and it says `a background
process is reading the screen`; **Stop** ends it.

Saving a profile records it as a `screen` line:

```
screen mirror 55 50 overlay
#      mode   boost smooth  overlay|solid
```

There is deliberately no display in that line: which monitor it was means
nothing on another machine, and the desktop remembers the local answer itself.
Loading such a profile — including the automatic reload at startup — loads the
settings and selects the mode, but **never starts the capture**: screen content
is a sensor, like the microphone, so only a person pressing Start switches it
on. The CLI ignores the line, so the profile still works with `g810-led -p`.

The tray icon needs an appindicator library at build time — on Debian/Ubuntu
`libayatana-appindicator3-dev`, on Arch `libayatana-appindicator`, on Fedora
`libayatana-appindicator-gtk3-devel`. Without it the GUI builds and closing the
window behaves as described under *Screen colors* and the other live effects.

Screen capture needs PipeWire and glibmm at build time — on Debian/Ubuntu
`libpipewire-0.3-dev` and `libglibmm-2.4-dev`, on Arch `libpipewire` and
`glibmm`, on Fedora `pipewire-devel` and `glibmm24-devel` — and, at run time, a
desktop with an `xdg-desktop-portal` ScreenCast backend (KDE, GNOME, wlroots
and others ship one). Without the build dependencies the GUI still builds and
that one effect reports that it was compiled out.

### Known limits

This is an experiment, and the honest list is more useful than a feature
table. Everything here was found by trying it on real hardware.

**Ripple is not a ripple on every keyboard.** It is an effect the firmware
runs, not something this program draws, and the firmwares differ. On a G512
it floods the board with the colour and twinkles one key at a time; nothing
responds to typing. The row says so rather than promising rings.

**A keyboard on Bluetooth cannot be lit.** It accepts every colour command
and quietly ignores it — Logitech's own software will not light one over
Bluetooth either. Such a keyboard is listed and clearly marked, and choosing
it explains what to do instead: use the USB cable, or the Lightspeed
receiver.

**Wireless keyboards need their features discovered.** HID++ does not fix
where a device keeps its lighting, so the numbers that work for a wired
G815 address something else entirely on a G915. The library now asks the
keyboard where its features are before addressing them, which is what makes
a G915 on its Lightspeed receiver work at all. Only the G915 has been
tested this way; other wireless models may need the same treatment and have
not been tried.

**Memory and everything else is read-only.** The DIMM list identifies your
sticks from the SPD the kernel already publishes — no root, no raw bus
access. Lighting RGB memory would mean writing to the SMBus, which on some
boards can hang the machine and, at a wrong address, corrupt an SPD and
cost you a stick. That is not a risk worth taking for an experiment. If
controlling RAM is what you want, [OpenRGB](https://openrgb.org) does it
properly.

**Only one keyboard is driven at a time.** You can switch which, but the
colours go to the selected one.

**The app cannot read your keyboard.** These keyboards are write-only for# g810-led</br>

Linux led controller for Logitech G213, G410, G413, G512, G513, G610, G810, G815, G910 and GPRO Keyboards.</br>

## Compatible keyboards :</br>
- **G213 Prodigy**</br>
- **G410 Atlas Spectrum**</br>
- **G413 Carbon**</br>
- **G512 Carbon**</br>
- **G513 Carbon**</br>
- **G610 Orion Brown**</br>
- **G610 Orion Red**</br>
- **G810 Orion Spectrum**</br>
- **G815 LIGHTSYNC**</br>
- **G910 Orion Spark**</br>
- **G910 Orion Spectrum**</br>
- **GPRO**</br>

## Contribute and evolution :</br>
* [CONTRIBUTING.md](https://github.com/MatMoul/g810-led/blob/master/CONTRIBUTING.md)

## Install :</br>
* [INSTALL.md](https://github.com/MatMoul/g810-led/blob/master/INSTALL.md)

## Profiles :<br>
You can load predefined configurations on startup! 
* [PROFILES.md](https://github.com/MatMoul/g810-led/blob/master/PROFILES.md)

## Help :</br>
`g213-led --help`</br>
`g410-led --help`</br>
`g413-led --help`</br>
`g512-led --help`</br>
`g513-led --help`</br>
`g610-led --help`</br>
`g810-led --help`</br>
`g815-led --help`</br>
`g910-led --help`</br>
`gpro-led --help`</br>

`g810-led --help-keys`</br>
`g810-led --help-effects`</br>
`g810-led --help-samples`</br>

## Samples :</br>
`g810-led -p /etc/g810-led/profile # Load a profile`</br>
`g810-led -k logo ff0000 # Set color of a key`</br>
`g810-led -a 00ff00 # Set color of all keys`</br>
`g810-led -g fkeys ff00ff # Set color of a group of keys`</br>
`g810-led --startup-mode color # Set keyboard power on effect`</br>
`g810-led -fx color keys 00ff00 # Set fixed color effect`</br>
`g810-led -fx breathing logo 00ff00 0a # Set breathing effect`</br>
`g810-led -fx cycle all 0a # Set color cycle effect`</br>
`g810-led -fx hwave keys 0a # Set horizontal wave effect`</br>
`g810-led -fx vwave keys 0a # Set vertical wave effect`</br>
`g810-led -fx cwave keys 0a # Set center wave effect`</br>

## Samples with no commit :</br>
`g810-led -an 000000 # Set color of all key with no action`</br>
`g810-led -gn modifiers ff0000 # Set color of a group with no action`</br>
`g810-led -kn w ff0000 # Set color of a key with no action`</br>
`g810-led -kn a ff0000 # Set color of a key with no action`</br>
`g810-led -kn s ff0000 # Set color of a key with no action`</br>
`g810-led -kn d ff0000 # Set color of a key with no action`</br>
`g810-led -c # Commit all changes`</br>

## Samples for G610 :</br>
`g610-led -a 60 # Set intensity of all keys`</br>
`g610-led -k logo ff # Set intensity of a key`</br>
`g610-led -g fkeys aa # Set intensity of a group of keys`</br>

## Samples for G213 :</br>
`g213-led -a 00ff00 # Set all keys green`</br>
`g213-led -r 1 ff0000 # Set region 1 red`</br>

## Samples with pipe (for effects) :</br>
`g810-led -pp < profilefile # Load a profile`</br>
`echo -e "k w ff0000\nk a ff0000\nk s ff0000\nk d ff0000\nc" | g810-led -pp # Set multiple keys`</br>

## Testing unsupported keyboards :</br>
Start by retrieving the VendorID and the ProductID of your keyboard using lsusb.</br>
`lsusb`</br>
Sample return :<br>
`Bus 001 Device 001: ID 046d:c331 Logitech, Inc.`</br>
In this sample VendorID is 046d and ProductID is c331. Now test your keyboard with all supported protocol (for 2019 keyboard start with -tuk 4):</br>
`g810-led -dv 046d -dp c331 -tuk 1 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (1), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 2 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (2), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 3 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (2), if not continue.</br>
`g810-led -dv 046d -dp c331 -tuk 4 -a 000000`</br>
If your keyboard set all key to off you have found the protocol (3), if not, need new dump.</br>

## Building and linking against the libg810-led library :</br>
Include in implementing source files.</br>
```cpp
#include <g810-led/Keyboard.h>
```
To link, simply provide `-lg810-led` to the build flags.</br>

To build the g810-led application as a dynamically-linked variant, run the target:</br>
`make bin-linked`</br>

## Dumps :
Dumps of keyboards are now stored in a separate project to preserve a small download size of this project.
You can find them here : [https://github.com/MatMoul/g810-led-resources](https://github.com/MatMoul/g810-led-resources)

## GUI :

`g810-led-gui` is a graphical front end for this library: a picture of your
keyboard that you paint, and the effects the keyboard and this computer can
run. It is an experiment rather than a finished product — it started as
"what would g810-led look like with a window?" and is being improved as
things turn out to be wrong. The command line is unchanged and keeps
working exactly as it did.

### Building and running

You need a C++11 compiler, `gtkmm-3.0`, `libepoxy` and hidapi. Optional, each
enabling one thing:

| package | what you lose without it |
|---|---|
| `libpulse` | sound-reactive lighting |
| `libpipewire-0.3` + `glibmm-2.4` | screen colors |
| `ayatana-appindicator3` (or `appindicator3`) | the tray icon |

```sh
make gui          # builds bin/g810-led-gui
./bin/g810-led-gui
```

Anything missing is compiled out and the window says so where that feature
would have been, rather than failing to build.

Your keyboard has to be writable by your user. This project ships udev rules
that rely on `uaccess`; if the window says the device is closed to you, it
prints the exact rule and the command to install it.

### First run

The window opens on your keyboard, drawn in 3D. Pick a colour, click keys to
paint them, and press **Send to keyboard**. Nothing reaches the hardware until
you do — everything before that is a draft you can undo.

The one habit worth learning: the strip above the tabs is the truth. It says
which keyboard is connected, what the keys are showing *right now*, and
whether you have anything unsent.

### What it does

The window is organised around what you are trying to do. Along the very top is
a **File** menu — open colors, the files you opened recently, paste colors as
text, save, save as, quit — a **Help** menu with the shortcut list, and at the
right end Undo and Redo. A strip above the tabs always says which
keyboard is connected, what the board is showing right now (your colors, an
effect, or a live effect) and how many changes are still unsent. Below it are
the four tabs:

* **Colors** — paint keys, key groups and regions. These are *staged*: nothing
  reaches the keyboard until you press **Send to keyboard** (Ctrl+Enter).
  Editing is undoable (Ctrl+Z / Ctrl+Shift+Z), hovering a key-group chip
  previews that group on the board, and right-clicking a key offers paint,
  clear, pick, and select-row / select-same-color.
* **On-board effects** — the effects the keyboard runs by itself. These apply
  immediately; turning one off puts your colors back.
* **Live effects** — raindrop, raindrop over your colors, wave, sound
  reactive and screen colors, driven by this computer. One runs at a time,
  and stopping it restores your colors.
* **Device** — which keyboard to talk to when more than one is plugged in,
  M/G keys, startup mode, lighting control, and an *Advanced* section for
  unsupported or test keyboards. Underneath is **Everything else in this
  machine**: your memory read from its own SPD, the USB devices from makers
  who ship lighting, and what Bluetooth knows about. That list is read-only
  and says of each thing whether this application can light it — which,
  mostly, it cannot. It is there to answer "is my hardware supported?"
  without guesswork.

Effects and live effects behave as previews: whatever they do to the board,
stopping them brings your scheme back. Ctrl+? lists the shortcuts.

### The board :
The keyboard on the left is a model of the one on your desk: the keys stand
proud of a brushed plate, the legends light up in the colour you give them, and
the light spills onto the plate around each cap the way it does on a G512.

* **drag with the right button** to turn it, **scroll** to zoom, **drag with the
  middle button** to slide it, **Home** to put it back
* right-click opens the key menu, double-click picks a colour, shift-click adds
  one key to the selection

The left button is a tool, and which one it is holding is the **Paint / Select**
toggle above the board:

* **Paint** — click a key to stage the current colour on it, or drag across
  keys to paint the ones you cross (one stroke, one undo). A drag that starts
  on the bare plate still rubber-bands a selection.
* **Select** — click a key to select just that one, or drag to rubber-band a
  group. Nothing is painted; use *Paint selection* when the group is right.

They were one gesture before, which meant a single key could not be selected
without also being painted.

The model is generated from the layout of whichever keyboard is connected, in
real millimetres (one key unit is 19.05 mm), so a TKL, a board with a G-key
column or one with a media cluster each come out as themselves rather than as a
G512 with pieces missing.

Drawing it needs an OpenGL 3.3 context, which comes from GTK and libepoxy — no
extra dependency. Where that is not available the flat grid of keys takes over
by itself and says so; the **3D board** checkbox under the keyboard switches
between the two at any time, and `G810_FORCE_2D=1` starts in the flat one.

### The tray icon :
Closing the window while a live effect is running does not stop it. The window
hides, the effect keeps running in the same process, and an icon appears in the
system tray whose menu carries the status and the controls worth having without
the whole window:

```
Board: screen colors · capturing screen (2560×1440)
G512 RGB MECHANICAL GAMING KEYBOARD
──────────
Show window
Stop effect
Live effect  ▸   Raindrop · Raindrop over your colors · Wave · Sound reactive · Screen colors
Mode         ▸   the modes of whichever effect is selected
──────────
Quit
```

The status line is the same sentence the strip at the top of the window shows,
and the two submenus are built from the window's own controls, so the tray can
never offer a different set of choices. **Quit** stops the effect and puts your
colors back before exiting — anything you want left running belongs in the tray,
not behind a closed program.

The icon uses StatusNotifierItem (through libayatana-appindicator), which is
what KDE, GNOME and most panels speak, and is the only tray protocol that works
under Wayland. Without an appindicator library at build time, or on a desktop
with no status-notifier host, the GUI checks for one at run time and falls back
to what it did before: raindrop and sound reactive are handed to a detached
background process, and screen colors asks whether to do the same.

### Sound reactive lighting :
The **Live effects** tab lights the keyboard from live audio on models with
per-key RGB. Choose *Sound reactive*, pick what to listen to — *Default output
(what you hear)* for music and games, *Default input* for the microphone, or
any source the sound server reports — then press **Start**. Modes:

* **Spectrum bars** — columns are frequency bands, the lit height of a column is that band's level
* **Rainbow spectrum** — a fixed hue per column, brightness follows the band
* **Level meter** — the whole board tracks loudness
* **Beat flash** — a flash on every detected beat

*Quiet*/*Loud* set the color gradient. Under *Fine tuning*, *Sensitivity*
scales the input and *Smoothing* controls how slowly a bar falls back after a
peak; *Auto gain* normalizes against the loudest recent peak, so quiet material
still fills the board — turn it off to drive the level from *Sensitivity*
alone. Mode, colors, sensitivity and smoothing can all be changed while it
runs.

**Blend over the current colors** (on by default) mixes the effect into the
key colors from the Colors tab instead of over black, so silence leaves the
board on your own scheme and the legends stay readable — the louder a key's
band, the more of the effect color it takes. Turn it off for the classic
black-background visualizer.

Closing the window leaves it running in the tray, where the menu shows what it
is listening to and **Stop effect** ends it. On a desktop with no tray the
effect is handed to a detached background process (`g810-led-gui
--audio-daemon`) instead, the same handoff the raindrop animation uses; open the
GUI again and it picks the running effect back up, with the controls showing its
settings, and **Stop** ends it and restores your colors.

Saving a profile records the effect as an `audio` line:

```
audio bars 0066ff ff0066 100 45 auto overlay alsa_output.hdmi-stereo.monitor
#     mode quiet  loud   gain smooth auto|fixed  overlay|solid  source (optional)
```

The key colors the effect blends over are the `a`/`k` lines of the same
profile, so a profile carries the scheme and the visualizer together.

Loading that profile — including the automatic reload of the last profile at
startup — turns sound-reactive lighting back on. `audio off` records that it
was not running. The CLI ignores the line, so such a profile still works with
`g810-led -p`. A source that no longer exists falls back to the default
output.

Audio capture needs libpulse (PulseAudio, or PipeWire through its PulseAudio
server) at build time — on Debian/Ubuntu `libpulse-dev`, on Arch `libpulse`,
on Fedora `pulseaudio-libs-devel`. Without it the GUI still builds and the
Audio tab reports that the feature was compiled out.

### Screen colors :
The **Live effects** tab can also paint the keyboard from what is on screen.
Choose *Screen colors* and press **Start**; your desktop asks which screen or
window to share, and remembers the answer, so later starts go straight to the
lights. *Choose a different screen…* forgets that answer and asks again.
Modes:

* **Mirror the screen** — each key takes the color of the part of the screen
  above it, so the board becomes a very low resolution copy of the display
* **Single dominant color** — one color for the whole board, weighted towards
  the colorful parts of the screen rather than its flat average, which on a
  normal desktop is a muddy grey

*Color boost* pushes what the keys show towards saturated color — screens are
mostly muted greys, and a keyboard reproducing that looks broken; 0 reproduces
the screen exactly. *Smoothing* controls how slowly a key follows the screen,
which is what stops video from strobing the board. **Blend over the current
colors** mixes the screen into the colors from the Colors tab instead of over
black, so the dark parts of the screen leave your own scheme showing. All of
these can be changed while it runs.

The state strip says `Board: screen colors · capturing …` in bold for as long
as it runs. Closing the window keeps it running and puts it in the tray, where
the icon and its status line say the screen is being read and **Stop effect**
is one click away — that visibility is the point, since this is the one effect
that reads your display.

On a desktop with no tray, closing **asks** instead: *Stop it* (the default)
ends the capture and puts your colors back, *Keep running* hands it to a
detached background process. Reopen the window then and it says `a background
process is reading the screen`; **Stop** ends it.

Saving a profile records it as a `screen` line:

```
screen mirror 55 50 overlay
#      mode   boost smooth  overlay|solid
```

There is deliberately no display in that line: which monitor it was means
nothing on another machine, and the desktop remembers the local answer itself.
Loading such a profile — including the automatic reload at startup — loads the
settings and selects the mode, but **never starts the capture**: screen content
is a sensor, like the microphone, so only a person pressing Start switches it
on. The CLI ignores the line, so the profile still works with `g810-led -p`.

The tray icon needs an appindicator library at build time — on Debian/Ubuntu
`libayatana-appindicator3-dev`, on Arch `libayatana-appindicator`, on Fedora
`libayatana-appindicator-gtk3-devel`. Without it the GUI builds and closing the
window behaves as described under *Screen colors* and the other live effects.

Screen capture needs PipeWire and glibmm at build time — on Debian/Ubuntu
`libpipewire-0.3-dev` and `libglibmm-2.4-dev`, on Arch `libpipewire` and
`glibmm`, on Fedora `pipewire-devel` and `glibmm24-devel` — and, at run time, a
desktop with an `xdg-desktop-portal` ScreenCast backend (KDE, GNOME, wlroots
and others ship one). Without the build dependencies the GUI still builds and
that one effect reports that it was compiled out.

### Known limits

This is an experiment, and the honest list is more useful than a feature
table. Everything here was found by trying it on real hardware.

**Ripple is not a ripple on every keyboard.** It is an effect the firmware
runs, not something this program draws, and the firmwares differ. On a G512
it floods the board with the colour and twinkles one key at a time; nothing
responds to typing. The row says so rather than promising rings.

**A keyboard on Bluetooth cannot be lit.** It accepts every colour command
and quietly ignores it — Logitech's own software will not light one over
Bluetooth either. Such a keyboard is listed and clearly marked, and choosing
it explains what to do instead: use the USB cable, or the Lightspeed
receiver.

**Wireless keyboards need their features discovered.** HID++ does not fix
where a device keeps its lighting, so the numbers that work for a wired
G815 address something else entirely on a G915. The library now asks the
keyboard where its features are before addressing them, which is what makes
a G915 on its Lightspeed receiver work at all. Only the G915 has been
tested this way; other wireless models may need the same treatment and have
not been tried.

**Memory and everything else is read-only.** The DIMM list identifies your
sticks from the SPD the kernel already publishes — no root, no raw bus
access. Lighting RGB memory would mean writing to the SMBus, which on some
boards can hang the machine and, at a wrong address, corrupt an SPD and
cost you a stick. That is not a risk worth taking for an experiment. If
controlling RAM is what you want, [OpenRGB](https://openrgb.org) does it
properly.

**Only one keyboard is driven at a time.** You can switch which, but the
colours go to the selected one.

**The app cannot read your keyboard.** These keyboards are write-only for
per-key colour, so what the keys are showing is something the program knows
only because it put it there. On startup it sends your last colours so that
the picture and the keyboard actually agree, unless a live effect is already
running — in which case it leaves the keys alone and says so.

### Contributing

Bug reports are most useful with the keyboard model, how it is connected
(USB, receiver or Bluetooth) and what the Device tab's **Diagnostics** fold
says — it is copyable in one piece. If a keyboard is nearly working, the
interesting question is usually which HID++ feature indices it reports;
that is the thing that differs between models that otherwise speak the same
protocol.

per-key colour, so what the keys are showing is something the program knows
only because it put it there. On startup it sends your last colours so that
the picture and the keyboard actually agree, unless a live effect is already
running — in which case it leaves the keys alone and says so.

### Contributing

Bug reports are most useful with the keyboard model, how it is connected
(USB, receiver or Bluetooth) and what the Device tab's **Diagnostics** fold
says — it is copyable in one piece. If a keyboard is nearly working, the
interesting question is usually which HID++ feature indices it reports;
that is the thing that differs between models that otherwise speak the same
protocol.
