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

#ifndef KEYBOARD_CLASS
#define KEYBOARD_CLASS

#include <chrono>
#include <iostream>
#include <vector>

#if defined(hidapi)
	#include "hidapi/hidapi.h"
#elif defined(libusb)
	#include "libusb-1.0/libusb.h"
#endif


class LedKeyboard {
	
	
	private:
		
		enum class KeyAddressGroup : uint8_t {
			logo = 0x00,
			indicators,
			multimedia,
			gkeys,
			keys
		};
		
		
	public:
		
		std::vector<std::vector<uint16_t>> SupportedKeyboards = {
			{ 0x46d, 0xc336, (uint16_t)KeyboardModel::g213 },
			{ 0x46d, 0xc330, (uint16_t)KeyboardModel::g410 },
			{ 0x46d, 0xc33a, (uint16_t)KeyboardModel::g413 },
			{ 0x46d, 0xc342, (uint16_t)KeyboardModel::g512 },
			{ 0x46d, 0xc33c, (uint16_t)KeyboardModel::g513 },
			{ 0x46d, 0xc333, (uint16_t)KeyboardModel::g610 },
			{ 0x46d, 0xc338, (uint16_t)KeyboardModel::g610 },
			{ 0x46d, 0xc331, (uint16_t)KeyboardModel::g810 },
			{ 0x46d, 0xc337, (uint16_t)KeyboardModel::g810 },
			{ 0x46d, 0xc33f, (uint16_t)KeyboardModel::g815 },
			// The G915, reached through its Lightspeed receiver. It is
			// the G815's wireless sibling and speaks the same protocol —
			// once the packets are addressed where this keyboard keeps
			// its features rather than where a G815 does, which is what
			// discoverFeatures() is for.
			{ 0x46d, 0x407c, (uint16_t)KeyboardModel::g815 },
			// Its Bluetooth identity stays out. It is the G815's wireless
			// sibling and answers on 0xb354 over Bluetooth and 0x407c
			// through its Lightspeed receiver, and both were tried: every
			// write is accepted and the keyboard carries on showing its
			// own on-board preset. This table means "the library can
			// drive it", so listing it here would put a target in front
			// of the user that quietly does nothing.
			//
			// The cause looks like the device index. Every packet here
			// goes out as 0x11 0xff ..., and that 0xff addresses the
			// receiver rather than the keyboard paired to it; a wired
			// keyboard is the only device on its own link, so it never
			// mattered before. Driving one of these means addressing it
			// by its real index, which is work this table cannot express.
			{ 0x46d, 0xc32b, (uint16_t)KeyboardModel::g910 },
			{ 0x46d, 0xc335, (uint16_t)KeyboardModel::g910 },
			{ 0x46d, 0xc339, (uint16_t)KeyboardModel::gpro }
		};
		
		enum class KeyboardModel : uint8_t {
			unknown = 0x00,
			g213,
			g410,
			g413,
			g512,
			g513,
			g610,
			g810,
			g815,
			g910,
			gpro
		};
		enum class StartupMode : uint8_t {
			 // TODO: On the G Pro, the value 1 selects the
			 // user-stored lighting effect, which is not
			 // necessarily wave.
			wave = 0x01,
			color
		};
		enum class OnBoardMode : uint8_t {
			board = 0x01,
			software
		};
		enum class NativeEffectGroup : uint8_t {
			off,
			color,
			breathing,
			cycle,
			waves,
			ripple
		};
		enum class NativeEffect : uint16_t {
			off,
			color = static_cast<uint16_t>(NativeEffectGroup::color) << 8,
			breathing = static_cast<uint16_t>(NativeEffectGroup::breathing) << 8,
			cycle = static_cast<uint16_t>(NativeEffectGroup::cycle) << 8,
			waves = static_cast<uint16_t>(NativeEffectGroup::waves) << 8,
			hwave,
			vwave,
			cwave,
			ripple = static_cast<uint16_t>(NativeEffectGroup::ripple) << 8
		};
		enum class NativeEffectPart : uint8_t {
			all = 0xff,
			keys = 0x00,
			logo
		};
		enum class NativeEffectStorage : uint8_t {
			none = 0x00,
			// "user-stored lighting" can be recalled with backlight+7
			user,
		};
		enum class KeyGroup : uint8_t {
			logo = 0x00,
			indicators,
			multimedia,
			gkeys,
			fkeys,
			modifiers,
			functions,
			arrows,
			numeric,
			keys
		};
		enum class Key : uint16_t { // 127 items
			
			logo = static_cast<uint8_t>(KeyAddressGroup::logo) << 8 | 0x01,
			logo2,
			
			backlight = static_cast<uint8_t>(KeyAddressGroup::indicators) << 8| 0x01,
			game, caps, scroll, num, 
			
			next = static_cast<uint8_t>(KeyAddressGroup::multimedia) << 8 | 0xb5,
			prev, stop,
			play = static_cast<uint8_t>(KeyAddressGroup::multimedia) << 8 | 0xcd,
			mute = static_cast<uint8_t>(KeyAddressGroup::multimedia) << 8 | 0xe2,
			
			g1 = static_cast<uint8_t>(KeyAddressGroup::gkeys) << 8 | 0x01,
			g2, g3, g4, g5, g6, g7, g8, g9,
			
			a = static_cast<uint8_t>(KeyAddressGroup::keys) << 8 | 0x04,
			b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
			n1, n2, n3, n4, n5, n6, n7, n8, n9, n0,
			enter, esc, backspace, tab, space, minus, equal, open_bracket, close_bracket,
			backslash, dollar, semicolon, quote, tilde, comma, period, slash, caps_lock,
			f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
			print_screen, scroll_lock, pause_break, insert, home, page_up, del, end, page_down,
			arrow_right, arrow_left, arrow_bottom, arrow_top, num_lock, num_slash, num_asterisk,
			num_minus, num_plus, num_enter,
			num_1, num_2, num_3, num_4, num_5, num_6, num_7, num_8, num_9, num_0,
			num_dot, intl_backslash, menu,

			abnt_slash = static_cast<uint8_t>(KeyAddressGroup::keys) << 8 | 0x87,
			
			ctrl_left = static_cast<uint8_t>(KeyAddressGroup::keys) << 8 | 0xe0,
			shift_left, alt_left, win_left,
			ctrl_right, shift_right, alt_right, win_right
			
		};

		typedef struct {
			uint16_t vendorID = 0x0;
			uint16_t productID = 0x0;
			// How it is attached. A wireless keyboard reached over
			// Bluetooth takes every lighting write and does nothing with
			// it — the transport accepts the report and the firmware
			// ignores it — so the difference has to be visible to anyone
			// deciding whether this device can be driven.
			bool bluetooth = false;
			std::string manufacturer = "";
			std::string product = "";
			std::string serialNumber = "";
			KeyboardModel model;
		} DeviceInfo;
		
		struct Color {
			uint8_t red;
			uint8_t green;
			uint8_t blue;
		};
		struct KeyValue {
			LedKeyboard::Key key;
			LedKeyboard::Color color;
		};
		
		typedef std::vector<KeyValue> KeyValueArray;
		
		
		~LedKeyboard();
		
		
		std::vector<DeviceInfo> listKeyboards();
		
		bool isOpen();
		bool open();
		bool open(uint16_t vendorID, uint16_t productID, std::string serial);
		DeviceInfo getCurrentDevice();
		bool close();
		
		KeyboardModel getKeyboardModel();
		
		bool commit();
		
		bool setKey(KeyValue keyValue);
		bool setKeys(KeyValueArray keyValues);
		bool setGroupKeys(KeyGroup keyGroup, Color color);
		bool setAllKeys(Color color);
		static std::vector<Key> keysForGroup(KeyGroup keyGroup);
		
		bool setMRKey(uint8_t value);
		bool setMNKey(uint8_t value);
		bool setGKeysMode(uint8_t value);
		
		bool setRegion(uint8_t region, Color color);
		bool setStartupMode(StartupMode startupMode);
		bool setOnBoardMode(OnBoardMode onBoardMode);

		bool setNativeEffect(NativeEffect effect, NativeEffectPart part,
				     std::chrono::duration<uint16_t, std::milli> period, Color color,
				     NativeEffectStorage storage);
		
		
	private:
		
		typedef std::vector<unsigned char> byte_buffer_t;
		typedef std::vector<Key> KeyArray;
		
		
		bool m_isOpen = false;
		DeviceInfo currentDevice;
		
		#if defined(hidapi)
			hid_device *m_hidHandle = NULL;
		#elif defined(libusb)
			bool m_isKernellDetached = false;
			libusb_device_handle *m_hidHandle = NULL;
			libusb_context *m_ctx = NULL;
		#endif
		
		
		bool sendDataInternal(byte_buffer_t &data);
		// Where this keyboard keeps its lighting and its profile switch.
		//
		// HID++ 2.0 does not fix these: a device carries a table and the
		// index has to be looked up in it. The numbers below are where a
		// wired G815 happens to keep them, and they were hardcoded until a
		// G915 turned up keeping the same two features at 0x0b and 0x15 —
		// every packet was being addressed to whatever else lived at 0x10,
		// accepted, and ignored. discoverFeatures() asks instead, and
		// leaves these alone on a device that will not answer.
		uint8_t featureLighting = 0x10;
		uint8_t featureOnBoard = 0x11;
		bool discoverFeatures();
		byte_buffer_t getKeyGroupAddress(KeyAddressGroup keyAddressGroup);
		
};

#endif
