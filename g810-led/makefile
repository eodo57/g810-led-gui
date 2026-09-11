CXX?=g++
CXXFLAGS?=-Wall -O2
LIB?=hidapi
ifeq ($(LIB),libusb)
	CPPFLAGS=-Dlibusb
	LIBS=-lusb-1.0
else
	CPPFLAGS=-Dhidapi
	LIBS=-lhidapi-hidraw
endif
SYSTEMDDIR?=/usr/lib/systemd

PREFIX?=$(DESTDIR)/usr
libdir?=$(PREFIX)/lib
includedir?=$(PREFIX)/include

# Program & versioning information
PROGN=g810-led
MAJOR=0
MINOR=4
MICRO=3

CXXFLAGS+=-std=gnu++11 -DVERSION=\"$(MAJOR).$(MINOR).$(MICRO)\"
APPSRCS=src/main.cpp src/helpers/*.cpp
LIBSRCS=src/classes/*.cpp
GUISRCS=gui/main.cpp gui/MainWindow.cpp gui/PanelColors.cpp gui/PanelEffects.cpp gui/PanelLive.cpp gui/PanelDevice.cpp gui/PanelStrip.cpp gui/KeyboardWidget.cpp gui/ProfileParser.cpp gui/Raindrop.cpp gui/Wave.cpp gui/WaveGraph.cpp gui/WavePlayer.cpp gui/FramePlayer.cpp gui/AudioCapture.cpp gui/AudioPlayer.cpp gui/ScreenCapture.cpp gui/ScreenPlayer.cpp gui/TrayIcon.cpp gui/KeyboardScene.cpp gui/ColorPicker.cpp gui/DeviceScan.cpp src/helpers/utils.cpp src/helpers/help.cpp
# Part of the GUI has no .cpp of its own: the style sheet and the color
# conversions are inline in headers. Those count as prerequisites, or
# editing one leaves a stale binary behind. They stay out of the compile
# line, which names the sources rather than taking every prerequisite.
GUIHDRS=$(wildcard gui/*.h) $(wildcard src/classes/*.h) $(wildcard src/helpers/*.h)
# Sound-reactive lighting needs libpulse (PulseAudio or PipeWire's
# PulseAudio server). Without it the GUI still builds; the audio tab
# just reports that the feature was compiled out.
HAVE_PULSE:=$(shell pkg-config --exists libpulse libpulse-simple && echo yes)
ifeq ($(HAVE_PULSE),yes)
	AUDIOCXXFLAGS=-DHAVE_PULSE $(shell pkg-config --cflags libpulse libpulse-simple)
	AUDIOLIBS=$(shell pkg-config --libs libpulse libpulse-simple)
endif
# Screen colors needs PipeWire for the frames; the desktop portal that
# hands them over is spoken to through giomm, which gtkmm already brings
# in. Same deal as the audio: without PipeWire the GUI builds and that
# one live effect reports that it was compiled out.
HAVE_PIPEWIRE:=$(shell pkg-config --exists libpipewire-0.3 giomm-2.4 && echo yes)
ifeq ($(HAVE_PIPEWIRE),yes)
	SCREENCXXFLAGS=-DHAVE_PIPEWIRE $(shell pkg-config --cflags libpipewire-0.3)
	SCREENLIBS=$(shell pkg-config --libs libpipewire-0.3)
endif
# The tray icon speaks StatusNotifierItem through an appindicator
# library; GtkStatusIcon is X11-only and does nothing under Wayland.
# Without one the GUI builds and closing the window behaves as it did
# before: the effect is handed to a background process.
TRAYPKG:=$(shell pkg-config --exists ayatana-appindicator3-0.1 && echo ayatana-appindicator3-0.1 || (pkg-config --exists appindicator3-0.1 && echo appindicator3-0.1))
ifneq ($(TRAYPKG),)
	TRAYCXXFLAGS=-DHAVE_APPINDICATOR $(shell pkg-config --cflags $(TRAYPKG))
	TRAYLIBS=$(shell pkg-config --libs $(TRAYPKG))
endif
GUICXXFLAGS=$(shell pkg-config --cflags gtkmm-3.0 epoxy) $(AUDIOCXXFLAGS) $(SCREENCXXFLAGS) $(TRAYCXXFLAGS) -Wno-deprecated-declarations
GUILIBS=$(shell pkg-config --libs gtkmm-3.0 epoxy) $(AUDIOLIBS) $(SCREENLIBS) $(TRAYLIBS)

.PHONY: all bin debug clean setup install uninstall lib install-lib install-dev gui

all: lib/lib$(PROGN).so bin/$(PROGN)

bin: bin/$(PROGN)

bin/$(PROGN): $(APPSRCS) $(LIBSRCS)
	@mkdir -p bin
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $^ -o $@ $(LIBS)
	
debug: CXXFLAGS += -g -Wextra -pedantic
debug: bin/$(PROGN)

lib/lib$(PROGN).so: $(LIBSRCS)
	@mkdir -p lib
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) -fPIC -shared -Wl,-soname,lib$(PROGN).so -o lib/lib$(PROGN).so.$(MAJOR).$(MINOR).$(MICRO) $^ $(LIBS)
	@ln -sf lib$(PROGN).so.$(MAJOR).$(MINOR).$(MICRO) lib/lib$(PROGN).so

bin-linked: lib/lib$(PROGN).so
	@mkdir -p bin
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $(APPSRCS) -o bin/$(PROGN) $(LIBS) -L./lib -l$(PROGN)

gui: bin/$(PROGN)-gui

bin/$(PROGN)-gui: $(GUISRCS) $(LIBSRCS) $(GUIHDRS)
	@mkdir -p bin
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(GUICXXFLAGS) $(GUISRCS) $(LIBSRCS) -o $@ $(GUILIBS) $(LIBS)

lib: lib/lib$(PROGN).so

clean:
	@rm -rf bin
	@rm -rf lib

setup:
	@install -m 755 -d \
		$(DESTDIR)/usr/bin \
		$(DESTDIR)/etc/$(PROGN)/samples \
		$(DESTDIR)/etc/udev/rules.d
	@cp bin/$(PROGN) $(DESTDIR)/usr/bin
	@test -s $(DESTDIR)/usr/bin/g213-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g213-led
	@test -s $(DESTDIR)/usr/bin/g410-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g410-led
	@test -s $(DESTDIR)/usr/bin/g413-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g413-led
	@test -s $(DESTDIR)/usr/bin/g512-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g512-led
	@test -s $(DESTDIR)/usr/bin/g513-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g513-led
	@test -s $(DESTDIR)/usr/bin/g610-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g610-led
	@test -s $(DESTDIR)/usr/bin/g815-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g815-led
	@test -s $(DESTDIR)/usr/bin/g910-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/g910-led
	@test -s $(DESTDIR)/usr/bin/gpro-led || ln -s /usr/bin/$(PROGN) $(DESTDIR)/usr/bin/gpro-led
	@cp sample_profiles/* $(DESTDIR)/etc/$(PROGN)/samples
	@cp udev/$(PROGN).rules $(DESTDIR)/etc/udev/rules.d
	@if [ -x bin/$(PROGN)-gui ]; then \
		cp bin/$(PROGN)-gui $(DESTDIR)/usr/bin; \
		install -m 755 -d $(DESTDIR)/usr/share/applications; \
		cp gui/$(PROGN)-gui.desktop $(DESTDIR)/usr/share/applications; \
	fi
	@if [ -x /usr/bin/systemd-run ]; then \
		install -m 755 -d $(DESTDIR)$(SYSTEMDDIR)/system; \
		cp systemd/$(PROGN)-reboot.service $(DESTDIR)$(SYSTEMDDIR)/system; \
	fi

install-lib: lib
	@install -m 755 -d $(libdir)
	@install -m 644 lib/lib$(PROGN).so.$(MAJOR).$(MINOR).$(MICRO) $(libdir)/
	@ln -sf lib$(PROGN).so.$(MAJOR).$(MINOR).$(MICRO) $(libdir)/lib$(PROGN).so

install-dev: install-lib
	@mkdir -p $(includedir)/$(PROGN)/
	@install -m 644 src/classes/*.h $(includedir)/$(PROGN)

install: setup
	@test -s /etc/$(PROGN)/profile || \
		cp /etc/$(PROGN)/samples/group_keys /etc/$(PROGN)/profile
	@test -s /etc/$(PROGN)/reboot || \
		cp /etc/$(PROGN)/samples/all_off /etc/$(PROGN)/reboot
	@udevadm control --reload-rules
	@$(PROGN) -p /etc/$(PROGN)/profile
	@if [ -x /usr/bin/systemd-run ]; then \
		systemctl daemon-reload; \
		systemctl enable $(PROGN)-reboot; \
	fi

uninstall-lib:
	@rm -f $(libdir)/lib$(PROGN).so*

uninstall-dev:
	@rm -rf $(includedir)/$(PROGN)

uninstall:
	@if [ -x /usr/bin/systemd-run ]; then \
		systemctl disable $(PROGN)-reboot; \
		rm -f $(SYSTEMDDIR)/system/$(PROGN)-reboot.service; \
		systemctl daemon-reload; \
	fi
	@rm -rf /etc/$(PROGN)
	
	@rm -f /usr/bin/g213-led
	@rm -f /usr/bin/g410-led
	@rm -f /usr/bin/g413-led
	@rm -f /usr/bin/g512-led
	@rm -f /usr/bin/g513-led
	@rm -f /usr/bin/g610-led
	@rm -f /usr/bin/g815-led
	@rm -f /usr/bin/g910-led
	@rm -f /usr/bin/gpro-led
	@rm -f /usr/bin/$(PROGN)
	@rm -f /usr/bin/$(PROGN)-gui
	@rm -f /usr/share/applications/$(PROGN)-gui.desktop
	
	@rm -f /etc/udev/rules.d/$(PROGN).rules
	@udevadm control --reload-rules
