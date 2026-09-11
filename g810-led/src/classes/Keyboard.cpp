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

#include "Keyboard.h"

#include <iostream>
#include <unistd.h>
#include <vector>
#include <map>
#include <cerrno>

#if defined(hidapi)
	#include <locale>
	#include "hidapi/hidapi.h"
#elif defined(libusb)
	#include "libusb-1.0/libusb.h"
#endif


using namespace std;


// wcstombs() does not terminate the buffer when the conversion fills it,
// and returns (size_t)-1 with the buffer in an unspecified state when a
// character has no representation in the current locale — in both cases
// treating the buffer as a C string reads past it. These strings come
// from the USB device (a hostile one can claim a supported id, and udev
// runs us as root on every hotplug), so bound and terminate explicitly.
static std::string fromDeviceString(const wchar_t *wide) {
	if (wide == NULL)
		return std::string();
	char buffer[256];
	size_t converted = wcstombs(buffer, wide, sizeof(buffer) - 1);
	if (converted == (size_t)-1)
		return std::string();
	buffer[converted] = '\0';
	return std::string(buffer);
}



LedKeyboard::~LedKeyboard() {
	close();
}


// What to call a keyboard that cannot name itself.
//
// A device behind a Lightspeed receiver is presented by the kernel as one
// of its own, but it inherits the receiver's strings, so it reports "USB
// Receiver" — as a heading, as a chooser row, and in the status bar. The
// name is the only thing about it that is wrong.
static string nameFor(uint16_t vendorID, uint16_t productID,
                      const string &reported) {
	if (vendorID == 0x046d && productID == 0x407c) return "G915 (Lightspeed)";
	return reported;
}

vector<LedKeyboard::DeviceInfo> LedKeyboard::listKeyboards() {
	vector<LedKeyboard::DeviceInfo> deviceList;

	#if defined(hidapi)
		if (hid_init() < 0) return deviceList;
		
		struct hid_device_info *devs, *dev;
		devs = hid_enumerate(0x0, 0x0);
		dev = devs;
		while (dev) {
			for (size_t i = 0; i < SupportedKeyboards.size(); i++) {
				if (dev->vendor_id == SupportedKeyboards[i][0]) {
					if (dev->product_id == SupportedKeyboards[i][1]) {
						DeviceInfo deviceInfo;
						deviceInfo.vendorID=dev->vendor_id;
						deviceInfo.productID=dev->product_id;
						#if defined(HID_API_VERSION) && \
						    HID_API_VERSION >= HID_API_MAKE_VERSION(0, 13, 0)
							deviceInfo.bluetooth =
								(dev->bus_type == HID_API_BUS_BLUETOOTH);
						#endif

						deviceInfo.serialNumber = fromDeviceString(dev->serial_number);
						deviceInfo.manufacturer = fromDeviceString(dev->manufacturer_string);
						deviceInfo.product = nameFor(deviceInfo.vendorID,
							deviceInfo.productID,
							fromDeviceString(dev->product_string));

						deviceList.push_back(deviceInfo);
						dev = dev->next;
						break;
					}
				}
			}
			if (dev != NULL) dev = dev->next;
		}
		hid_free_enumeration(devs);
		hid_exit();
		
	#elif defined(libusb)
		// Enumeration must not touch m_ctx/m_hidHandle: those belong to
		// an open device, and this is routinely called (GUI rescan) while
		// one is open. It also used to init the member context but
		// enumerate with the uninitialised local one.
		libusb_context *ctx = NULL;
		if(libusb_init(&ctx) < 0) return deviceList;
		
		libusb_device **devs;
		ssize_t cnt = libusb_get_device_list(ctx, &devs);
		for(ssize_t i = 0; i < cnt; i++) {
			libusb_device *device = devs[i];
			libusb_device_descriptor desc;
			libusb_get_device_descriptor(device, &desc);
			for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
				if (desc.idVendor == SupportedKeyboards[i][0]) {
					if (desc.idProduct == SupportedKeyboards[i][1]) {
					  unsigned char buf[256];
						DeviceInfo deviceInfo;
						deviceInfo.vendorID=desc.idVendor;
						deviceInfo.productID=desc.idProduct;

						libusb_device_handle *handle = NULL;
						if (libusb_open(device, &handle) != 0)	continue;

						if (libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, buf, 256) >= 1) deviceInfo.serialNumber = string((char*)buf);
						if (libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, buf, 256) >= 1) deviceInfo.manufacturer = string((char*)buf);
						if (libusb_get_string_descriptor_ascii(handle, desc.iProduct, buf, 256) >= 1) deviceInfo.product = string((char*)buf);

						deviceList.push_back(deviceInfo);
						libusb_close(handle);
						break;
					}
				}
			}
		}
		libusb_free_device_list(devs, 1);
		libusb_exit(ctx);
	#endif
	
	return deviceList;
}


bool LedKeyboard::isOpen() {
	return m_isOpen;
}

bool LedKeyboard::open() {
	if (m_isOpen) return true;
	
	return open(0x0, 0x0, "");
}

bool LedKeyboard::open(uint16_t vendorID, uint16_t productID, string serial) {
	if (m_isOpen && ! close()) return false;
	currentDevice.model = KeyboardModel::unknown;

	#if defined(hidapi)
		if (hid_init() < 0) return false;

		struct hid_device_info *devs, *dev;
		devs = hid_enumerate(vendorID, productID);
		dev = devs;
		wstring wideSerial;

		if (!serial.empty()) {
			// mbstowcs writes no terminator when the conversion exactly
			// fills the destination, so keep a slot for it: constructing
			// the wstring would otherwise read past the buffer.
			wchar_t tempSerial[256];
			size_t converted = mbstowcs(tempSerial, serial.c_str(),
			                            sizeof(tempSerial) / sizeof(tempSerial[0]) - 1);
			if (converted == (size_t)-1 || converted < 1) return false;
			tempSerial[converted] = L'\0';
			wideSerial = wstring(tempSerial);
		}

		while (dev) {
			for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
				if (dev->vendor_id == SupportedKeyboards[i][0] && dev->product_id == SupportedKeyboards[i][1]) {
					// A device that publishes no serial cannot satisfy a
					// request for a specific one; it used to pass the filter.
					if (!serial.empty() && (dev->serial_number == NULL ||
					    wideSerial.compare(dev->serial_number) != 0)) break;

					currentDevice.serialNumber = fromDeviceString(dev->serial_number);
					currentDevice.manufacturer = fromDeviceString(dev->manufacturer_string);
					// From the enumeration, not from currentDevice: the
					// two ids below are not filled in until the next
					// lines, so asking those would be asking what this
					// device was before it was this one.
					currentDevice.product = nameFor(dev->vendor_id,
						dev->product_id, fromDeviceString(dev->product_string));

					currentDevice.vendorID = dev->vendor_id;
					currentDevice.productID = dev->product_id;
					currentDevice.model = (KeyboardModel)SupportedKeyboards[i][2];
					break;
				}
			}
			if (currentDevice.model != KeyboardModel::unknown) break;
			dev = dev->next;
		}

		hid_free_enumeration(devs);

		if (! dev) {
			currentDevice.model = KeyboardModel::unknown;
			errno = ENODEV;

			hid_exit();
			return false;
		}

		if (wideSerial.empty()) m_hidHandle = hid_open(currentDevice.vendorID, currentDevice.productID, NULL);
		else m_hidHandle = hid_open(currentDevice.vendorID, currentDevice.productID, wideSerial.c_str());

		if(m_hidHandle == 0) {
			hid_exit();
			errno = EACCES;
			return false;
		}

		m_isOpen = true;
		// A wireless keyboard keeps its features wherever it likes, so ask
		// where before addressing them. Silence is harmless: a device that
		// does not answer keeps the wired defaults, which is what every
		// keyboard supported before this one used.
		discoverFeatures();
		return true;

	#elif defined(libusb)
		if (libusb_init(&m_ctx) < 0) return false;
			
		libusb_device **devs;
		libusb_device *dev = NULL;
		ssize_t cnt = libusb_get_device_list(m_ctx, &devs);
		if(cnt >= 0) {
			for(ssize_t i = 0; i < cnt; i++) {
				libusb_device *device = devs[i];
				libusb_device_descriptor desc;
				libusb_get_device_descriptor(device, &desc);

				if (vendorID != 0x0 && desc.idVendor != vendorID) continue;
				else if (productID != 0x0 && desc.idProduct != productID) continue;
				else if (! serial.empty()) {
					if (desc.iSerialNumber <= 0) continue; //Device does not populate serial number

					unsigned char buf[256];
					if (libusb_open(device, &m_hidHandle) != 0){
						m_hidHandle = NULL;
						continue;
					}

					if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iSerialNumber, buf, 256) >= 1 && serial.compare((char*)buf) == 0) {
						//Make sure entry is a supported keyboard and get model
						for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
							if (desc.idVendor == SupportedKeyboards[i][0]) {
								if (desc.idProduct == SupportedKeyboards[i][1]) {
									if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iManufacturer, buf, 256) >= 1) currentDevice.manufacturer = string((char*)buf);
									if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iProduct, buf, 256) >= 1) currentDevice.product = string((char*)buf);
									currentDevice.serialNumber = serial;
									currentDevice.vendorID = desc.idVendor;
									currentDevice.productID = desc.idProduct;
									currentDevice.model = (KeyboardModel)SupportedKeyboards[i][2];

									dev = device;
									libusb_close(m_hidHandle);
									m_hidHandle = NULL;
									break;
								}
							}
						}
					}
					else {
						libusb_close(m_hidHandle);
						m_hidHandle = NULL;
						continue; //Serial number set but doesn't match
					}
				}

				//For the case where serial is not specified, find first supported device
				for (int i=0; i<(int)SupportedKeyboards.size(); i++) {
					if (desc.idVendor == SupportedKeyboards[i][0]) {
						if (desc.idProduct == SupportedKeyboards[i][1]) {
							unsigned char buf[256];
							if (libusb_open(device, &m_hidHandle) != 0){
								m_hidHandle = NULL;
								continue;
							}
							currentDevice.vendorID = desc.idVendor;
							currentDevice.productID = desc.idProduct;
							currentDevice.model = (KeyboardModel)SupportedKeyboards[i][2];
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iManufacturer, buf, 256) >= 1) currentDevice.manufacturer = string((char*)buf);
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iProduct, buf, 256) >= 1) currentDevice.product = string((char*)buf);
							if (libusb_get_string_descriptor_ascii(m_hidHandle, desc.iSerialNumber, buf, 256) >= 1) currentDevice.serialNumber = string((char*)buf);

							libusb_close(m_hidHandle);
							m_hidHandle=NULL;
							break;
						}
					}
				}
				if (currentDevice.model != KeyboardModel::unknown) break;
			}
			libusb_free_device_list(devs, 1);
		}


		if (currentDevice.model == KeyboardModel::unknown) {
			libusb_exit(m_ctx);
			errno = ENODEV;
			m_ctx = NULL;
			return false;
		}
			
		if (dev == NULL) m_hidHandle = libusb_open_device_with_vid_pid(m_ctx, currentDevice.vendorID, currentDevice.productID);
		else libusb_open(dev, &m_hidHandle);

		if(m_hidHandle == NULL) {
			libusb_exit(m_ctx);
			errno = EACCES;
			m_ctx = NULL;
			return false;
		}
			
		if(libusb_kernel_driver_active(m_hidHandle, 1) == 1) {
			if(libusb_detach_kernel_driver(m_hidHandle, 1) != 0) {
				libusb_exit(m_ctx);
				errno = EACCES;
				m_ctx = NULL;
				return false;
			}
			m_isKernellDetached = true;
		}
			
		if(libusb_claim_interface(m_hidHandle, 1) < 0) {
			if(m_isKernellDetached==true) {
				libusb_attach_kernel_driver(m_hidHandle, 1);
				m_isKernellDetached = false;
			}
			libusb_exit(m_ctx);
			errno = EACCES;
			m_ctx = NULL;
			return false;
		}
			
		m_isOpen = true;
		return true;
	#endif

	return false; //In case neither is defined
}

LedKeyboard::DeviceInfo LedKeyboard::getCurrentDevice() {
	return currentDevice;
}

bool LedKeyboard::close() {
	if (! m_isOpen) return true;
	m_isOpen = false;
	
	#if defined(hidapi)
		hid_close(m_hidHandle);
		m_hidHandle = NULL;
		hid_exit();
		return true;
	#elif defined(libusb)
		if (m_hidHandle == NULL) return true;
		if(libusb_release_interface(m_hidHandle, 1) != 0) return false;
		if(m_isKernellDetached==true) {
			libusb_attach_kernel_driver(m_hidHandle, 1);
			m_isKernellDetached = false;
		}
		libusb_close(m_hidHandle);
		m_hidHandle = NULL;
		libusb_exit(m_ctx);
		m_ctx = NULL;
		return true;
	#endif
	
	return false;
}


LedKeyboard::KeyboardModel LedKeyboard::getKeyboardModel() {
	return currentDevice.model;
}

bool LedKeyboard::commit() {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
			return true; // Keyboard is non-transactional
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			data = { 0x11, 0xff, 0x0c, 0x5a };
			break;
		case KeyboardModel::g815:
			data = { 0x11, 0xff, featureLighting, 0x7f };
			break;
		case KeyboardModel::g910:
			data = { 0x11, 0xff, 0x0f, 0x5d };
			break;
		default:
			return false;
	}
	data.resize(20, 0x00);
	return sendDataInternal(data);
}

bool LedKeyboard::setKey(LedKeyboard::KeyValue keyValue) {
	return setKeys(KeyValueArray {keyValue});
}

bool LedKeyboard::setKeys(KeyValueArray keyValues) {
	if (keyValues.empty()) return false;
	
	bool retval = true;
	
	vector<vector<KeyValue>> SortedKeys;
	map<int32_t, vector<KeyValue>> KeyByColors;
	map<int32_t, vector<KeyValue>>::iterator KeyByColorsIterator;
	const uint8_t maxKeyPerColor = 13;
	
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			for (size_t i = 0; i < keyValues.size(); i++) {
				uint32_t colorkey = static_cast<uint32_t>(keyValues[i].color.red | keyValues[i].color.green << 8 | keyValues[i].color.blue << 16 );
				if (KeyByColors.count(colorkey) == 0) KeyByColors.insert(pair<uint32_t, vector<KeyValue>>(colorkey, {}));
				KeyByColors[colorkey].push_back(keyValues[i]);
			}
			
			for (auto& x: KeyByColors) {
				if (x.second.size() > 0) {
					size_t gi = 0;
					while (gi < x.second.size()) {
						size_t data_size = 20;
						byte_buffer_t data = { 0x11, 0xff, featureLighting, 0x6c };
						data.push_back(x.second[0].color.red);
						data.push_back(x.second[0].color.green);
						data.push_back(x.second[0].color.blue);
						for (uint8_t i = 0; i < maxKeyPerColor; i++) {
							if (gi + i < x.second.size()) {
								switch (x.second[gi+i].key) {
									case Key::logo2:
									case Key::game:
									case Key::caps:
									case Key::scroll:
									case Key::num:
									case Key::stop:
									case Key::g6:
									case Key::g7:
									case Key::g8:
									case Key::g9:
										break;
									case Key::play:
										data.push_back(0x9b);
										break;
									case Key::mute:
										data.push_back(0x9c);
										break;
									case Key::next:
										data.push_back(0x9d);
										break;
									case Key::prev:
										data.push_back(0x9e);
										break;
									case Key::ctrl_left:
									case Key::shift_left:
									case Key::alt_left:
									case Key::win_left:
									case Key::ctrl_right:
									case Key::shift_right:
									case Key::alt_right:
									case Key::win_right:
										data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) - 0x78);
										break;
									default:
										switch (static_cast<KeyAddressGroup>((static_cast<uint16_t>(x.second[gi+i].key) & 0xff00) / 0xff)) {
											case KeyAddressGroup::logo:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0xd1);
												break;
											case KeyAddressGroup::indicators:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0x98);
												break;
											case KeyAddressGroup::gkeys:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) + 0xb3);
												break;
											case KeyAddressGroup::keys:
												data.push_back((static_cast<uint8_t>(x.second[gi+i].key) & 0x00ff) - 0x03);
												break;
											default:
												break;
										}
								}
							}
						}
						
						if (data.size() < data_size) data.push_back(0xff);
						data.resize(data_size, 0x00);
						if (retval) retval = sendDataInternal(data);
						else sendDataInternal(data);
						
						gi = gi + maxKeyPerColor;
					}
				}
			}
			
			break;
		default:
			SortedKeys = {
				{}, // Logo AddressGroup
				{}, // Indicators AddressGroup
				{}, // Multimedia AddressGroup
				{}, // GKeys AddressGroup
				{} // Keys AddressGroup
			};
			
			for (size_t i = 0; i < keyValues.size(); i++) {
				switch(static_cast<LedKeyboard::KeyAddressGroup>(static_cast<uint16_t>(keyValues[i].key) >> 8 )) {
					case LedKeyboard::KeyAddressGroup::logo:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[0].size() <= 1 && keyValues[i].key == LedKeyboard::Key::logo)
									SortedKeys[0].push_back(keyValues[i]);
								break;
							case LedKeyboard::KeyboardModel::g910:
								if (SortedKeys[0].size() <= 2) SortedKeys[0].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::indicators:
						if (SortedKeys[1].size() <= 5) SortedKeys[1].push_back(keyValues[i]);
						break;
					case LedKeyboard::KeyAddressGroup::multimedia:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[2].size() <= 5) SortedKeys[2].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::gkeys:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g910:
								if (SortedKeys[3].size() <= 9) SortedKeys[3].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
					case LedKeyboard::KeyAddressGroup::keys:
						switch (currentDevice.model) {
							case LedKeyboard::KeyboardModel::g512:
							case LedKeyboard::KeyboardModel::g513:
							case LedKeyboard::KeyboardModel::g610:
							case LedKeyboard::KeyboardModel::g810:
							case LedKeyboard::KeyboardModel::g910:
							case LedKeyboard::KeyboardModel::gpro:
								if (SortedKeys[4].size() <= 120) SortedKeys[4].push_back(keyValues[i]);
								break;
							case LedKeyboard::KeyboardModel::g410:
								if (SortedKeys[4].size() <= 120)
									if (keyValues[i].key < LedKeyboard::Key::num_lock ||
										keyValues[i].key > LedKeyboard::Key::num_dot)
										SortedKeys[4].push_back(keyValues[i]);
								break;
							default:
								break;
						}
						break;
				}
			}
			
			for (uint8_t kag = 0; kag < 5; kag++) {
				
				if (SortedKeys[kag].size() > 0) {
					
					size_t gi = 0;
					while (gi < SortedKeys[kag].size()) {
						
						size_t data_size = 0;
						byte_buffer_t data = {};
						
						switch (kag) {
							case 0:
								data_size = 20;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::logo);
								break;
							case 1:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::indicators);
								break;
							case 2:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::multimedia);
								break;
							case 3:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::gkeys);
								break;
							case 4:
								data_size = 64;
								data = getKeyGroupAddress(LedKeyboard::KeyAddressGroup::keys);
								break;
						}
						
						const uint8_t maxKeyCount = (data_size - 8) / 4;
						
						if (data.size() > 0) {
							
							for (uint8_t i = 0; i < maxKeyCount; i++) {
								if (gi + i < SortedKeys[kag].size()) {
									data.push_back(static_cast<uint8_t>(
										static_cast<uint16_t>(SortedKeys[kag][gi+i].key) & 0x00ff));
									data.push_back(SortedKeys[kag][gi+i].color.red);
									data.push_back(SortedKeys[kag][gi+i].color.green);
									data.push_back(SortedKeys[kag][gi+i].color.blue);
								}
							}
							
							data.resize(data_size, 0x00);
							
							if (retval) retval = sendDataInternal(data);
							else sendDataInternal(data);
							
						}
						
						gi = gi + maxKeyCount;
					}
					
				}
			}
	}
	
	return retval;
}

std::vector<LedKeyboard::Key> LedKeyboard::keysForGroup(KeyGroup keyGroup) {
	switch (keyGroup) {
		case KeyGroup::logo:
			return { Key::logo, Key::logo2 };
		case KeyGroup::indicators:
			return { Key::caps, Key::num, Key::scroll, Key::game, Key::backlight };
		case KeyGroup::multimedia:
			return { Key::next, Key::prev, Key::stop, Key::play, Key::mute };
		case KeyGroup::gkeys:
			return { Key::g1, Key::g2, Key::g3, Key::g4, Key::g5,
			         Key::g6, Key::g7, Key::g8, Key::g9 };
		case KeyGroup::fkeys:
			return { Key::f1, Key::f2, Key::f3, Key::f4, Key::f5, Key::f6,
			         Key::f7, Key::f8, Key::f9, Key::f10, Key::f11, Key::f12 };
		case KeyGroup::modifiers:
			return { Key::shift_left, Key::ctrl_left, Key::win_left, Key::alt_left,
			         Key::alt_right, Key::win_right, Key::ctrl_right, Key::shift_right,
			         Key::menu };
		case KeyGroup::functions:
			return { Key::esc, Key::print_screen, Key::scroll_lock, Key::pause_break,
			         Key::insert, Key::del, Key::home, Key::end, Key::page_up,
			         Key::page_down };
		case KeyGroup::arrows:
			return { Key::arrow_top, Key::arrow_left, Key::arrow_bottom, Key::arrow_right };
		case KeyGroup::numeric:
			return { Key::num_1, Key::num_2, Key::num_3, Key::num_4, Key::num_5,
			         Key::num_6, Key::num_7, Key::num_8, Key::num_9, Key::num_0,
			         Key::num_dot, Key::num_enter, Key::num_plus, Key::num_minus,
			         Key::num_asterisk, Key::num_slash, Key::num_lock };
		case KeyGroup::keys:
			return { Key::a, Key::b, Key::c, Key::d, Key::e, Key::f, Key::g, Key::h,
			         Key::i, Key::j, Key::k, Key::l, Key::m, Key::n, Key::o, Key::p,
			         Key::q, Key::r, Key::s, Key::t, Key::u, Key::v, Key::w, Key::x,
			         Key::y, Key::z,
			         Key::n1, Key::n2, Key::n3, Key::n4, Key::n5, Key::n6, Key::n7,
			         Key::n8, Key::n9, Key::n0,
			         Key::enter, Key::backspace, Key::tab, Key::space, Key::minus,
			         Key::equal, Key::open_bracket, Key::close_bracket, Key::backslash,
			         Key::dollar, Key::semicolon, Key::quote, Key::tilde, Key::comma,
			         Key::period, Key::slash, Key::caps_lock, Key::intl_backslash,
			         Key::abnt_slash };
		default:
			return {};
	}
}

bool LedKeyboard::setGroupKeys(KeyGroup keyGroup, LedKeyboard::Color color) {
	KeyValueArray keyValues;
	std::vector<Key> keyArray = keysForGroup(keyGroup);
	for (uint8_t i = 0; i < keyArray.size(); i++) keyValues.push_back({keyArray[i], color});
	return setKeys(keyValues);
}

bool LedKeyboard::setAllKeys(LedKeyboard::Color color) {
	KeyValueArray keyValues;

	switch (currentDevice.model) {
		case KeyboardModel::g213:
			for (uint8_t rIndex=0x01; rIndex <= 0x05; rIndex++) if (! setRegion(rIndex, color)) return false;
			return true;
		case KeyboardModel::g413:
			return setNativeEffect(NativeEffect::color, NativeEffectPart::keys,
					std::chrono::seconds(0), color, NativeEffectStorage::none);
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::g815:
		case KeyboardModel::g910:
		case KeyboardModel::gpro: {
			static const KeyGroup groups[] = {
				KeyGroup::logo, KeyGroup::indicators, KeyGroup::multimedia,
				KeyGroup::gkeys, KeyGroup::fkeys, KeyGroup::functions,
				KeyGroup::arrows, KeyGroup::numeric, KeyGroup::modifiers,
				KeyGroup::keys
			};
			for (KeyGroup group : groups) {
				std::vector<Key> keys = keysForGroup(group);
				for (Key key : keys) keyValues.push_back({key, color});
			}
			return setKeys(keyValues);
		}
		default:
			return false;
	}
	return false;
}


bool LedKeyboard::setMRKey(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x0c, 0x0c, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x0a, 0x0e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setMNKey(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			switch (value) {
				case 0x01:
                    data = { 0x11, 0xff, 0x0b, 0x1c, 0x01 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
                case 0x02:
                    data = { 0x11, 0xff, 0x0b, 0x1c, 0x02 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
                case 0x03:
                    data = { 0x11, 0xff, 0x0b, 0x1c, 0x04 };
                    data.resize(20, 0x00);
                    return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
				case 0x02:
				case 0x03:
				case 0x04:
				case 0x05:
				case 0x06:
				case 0x07:
					data = { 0x11, 0xff, 0x09, 0x1e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setGKeysMode(uint8_t value) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x0a, 0x2b, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		case KeyboardModel::g910:
			switch (value) {
				case 0x00:
				case 0x01:
					data = { 0x11, 0xff, 0x08, 0x2e, value };
					data.resize(20, 0x00);
					return sendDataInternal(data);
				default:
					break;
			}
			break;
		default:
			break;
	}
	return false;
}

bool LedKeyboard::setRegion(uint8_t region, LedKeyboard::Color color) {
	LedKeyboard::byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
			data = { 0x11, 0xff, 0x0c, 0x3a, region, 0x01, color.red, color.green, color.blue };
			data.resize(20,0x00);
			return sendDataInternal(data);
			break;
		default:
			break;
	}

	return false;
}

bool LedKeyboard::setStartupMode(StartupMode startupMode) {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610:
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			// g512/g513 speak the same protocol as the g810 and are
			// listed with poweronfx in help.h, but were missing here, so
			// the documented -s option always failed on them.
			data = { 0x11, 0xff, 0x0d, 0x5a, 0x00, 0x01 };
			break;
		case KeyboardModel::g910:
			data = { 0x11, 0xff, featureLighting, 0x5e, 0x00, 0x01 };
			break;
		default:
			return false;
	}
	data.push_back((unsigned char)startupMode);
	data.resize(20, 0x00);
	return sendDataInternal(data);
}

bool LedKeyboard::setOnBoardMode(OnBoardMode onBoardMode) {
	byte_buffer_t data;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			data = { 0x11, 0xff, featureOnBoard, 0x1a, static_cast<uint8_t>(onBoardMode) };
			data.resize(20, 0x00);
			return sendDataInternal(data);
		default:
			return false;
	}
}

bool LedKeyboard::setNativeEffect(NativeEffect effect, NativeEffectPart part,
				  std::chrono::duration<uint16_t, std::milli> period, Color color,
				  NativeEffectStorage storage) {
	uint8_t protocolBytes[2] = {0x00, 0x00};
	NativeEffectGroup effectGroup = static_cast<NativeEffectGroup>(static_cast<uint16_t>(effect) >> 8);

	// NativeEffectPart::all is not in the device protocol, but an alias for both keys and logo, plus indicators
	if (part == LedKeyboard::NativeEffectPart::all) {
		switch (effectGroup) {
			case NativeEffectGroup::color:
				if (! setGroupKeys(LedKeyboard::KeyGroup::indicators, color)) return false;
				if (! commit()) return false;
				break;
			case NativeEffectGroup::breathing:
				if (! setGroupKeys(LedKeyboard::KeyGroup::indicators, color)) return false;;
				if (! commit()) return false;;
				break;
			case NativeEffectGroup::cycle:
			case NativeEffectGroup::waves:
			case NativeEffectGroup::ripple:
				if (! setGroupKeys(
					LedKeyboard::KeyGroup::indicators,
					LedKeyboard::Color({0xff, 0xff, 0xff}))
				) return false;
				if (! commit()) return false;
				break;
			default:
				break;
		}
		return (
			setNativeEffect(effect, LedKeyboard::NativeEffectPart::keys, period, color, storage) &&
			setNativeEffect(effect, LedKeyboard::NativeEffectPart::logo, period, color, storage));
	}

	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
			protocolBytes[0] = 0x0c;
			protocolBytes[1] = 0x3c;
			if (part == NativeEffectPart::logo) return true; //Does not have logo component
			break;
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::g610: // Unconfirmed
		case KeyboardModel::g810:
		case KeyboardModel::gpro:
			protocolBytes[0] = 0x0d;
			protocolBytes[1] = 0x3c;
			break;
		case KeyboardModel::g815:
			protocolBytes[0] = 0x0f;
			protocolBytes[1] = 0x1c;
			break;
		case KeyboardModel::g910:
			protocolBytes[0] = 0x10;
			protocolBytes[1] = 0x3c;
			break;
		default:
			return false;
	}

	byte_buffer_t data = {
		0x11, 0xff, protocolBytes[0], protocolBytes[1],
		(uint8_t)part, static_cast<uint8_t>(effectGroup),
		// color of static-color and breathing effects
		color.red, color.green, color.blue,
		// period of breathing effect (ms)
		static_cast<uint8_t>(period.count() >> 8), static_cast<uint8_t>(period.count() & 0xff),
		// period of cycle effect (ms)
		static_cast<uint8_t>(period.count() >> 8), static_cast<uint8_t>(period.count() & 0xff),
		static_cast<uint8_t>(static_cast<uint16_t>(effect) & 0xff), // wave variation (e.g. horizontal)
		0x64, // unused?
		// period of wave effect (ms)
		static_cast<uint8_t>(period.count() >> 8), // LSB is shared with cycle effect above
		static_cast<uint8_t>(storage),
		0, // unused?
		0, // unused?
		0, // unused?
	};

	byte_buffer_t setupData;
	bool retval;
	switch (currentDevice.model) {
		case KeyboardModel::g815:
			setupData = { 0x11, 0xff, 0x0f, 0x5c, 0x01, 0x03, 0x03 };
			setupData.resize(20, 0x00);
			retval = sendDataInternal(setupData);

			data[16] = 0x01;

			switch (part) {
				case NativeEffectPart::keys:
					data[4] = 0x01;

					//Seems to conflict with a star-like effect on G410 and G810
					switch (effect) {
						case NativeEffect::ripple:
							//Adjust periodicity
							data[9]=0x00;
							data[10]=period.count() >> 8 & 0xff;;
							data[11]=period.count() & 0xff;
							data[12]=0x00;
							break;
						default:
							break;
					}
					break;
				case NativeEffectPart::logo:
					data[4] = 0x00;
					switch (effect) {
						case NativeEffect::breathing:
							data[5]=0x03;
							break;
						case NativeEffect::cwave:
						case NativeEffect::vwave:
						case NativeEffect::hwave:
							data[5]=0x02;
							data[13]=0x64;
							break;
						case NativeEffect::waves:
						case NativeEffect::cycle:
							data[5]=0x02;
							break;
						case NativeEffect::ripple:
						case NativeEffect::off:
							data[5]=0x00;
							break;
						default:
							data[5]=0x01;
							break;
					}
					break;
				default:
					break;
			}
			break;
		default: //Many devices may not support logo coloring for wave?
			if ((effectGroup == NativeEffectGroup::waves) && (part == NativeEffectPart::logo)) {
				return setNativeEffect(NativeEffect::color, part, std::chrono::seconds(0), Color({0x00, 0xff, 0xff}), storage);
			}
			break;
	}
	retval = sendDataInternal(data);
	return retval;
}


// Ask the keyboard where it keeps the two features this code drives.
//
// Only hidapi can do this: it needs a reply, and the libusb path here is
// write-only. A device that does not answer keeps the wired G815's
// numbers, which is what every keyboard supported before this did.
bool LedKeyboard::discoverFeatures() {
	#if defined(hidapi)
		if (!m_isOpen || !m_hidHandle) return false;
		// Byte 3 of a request is (function << 4) | a software id of our
		// choosing, and the reply echoes it, which is what tells our
		// answer apart from the traffic a keyboard sends unprompted.
		const uint8_t software = 0x05;
		struct { uint16_t feature; uint8_t *into; } wanted[] = {
			{ 0x8081, &featureLighting },   // per-key lighting
			{ 0x8100, &featureOnBoard },    // on-board profiles
		};
		bool answered = false;
		for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
			uint8_t out[7] = { 0x10, 0xff, 0x00, (uint8_t)((0 << 4) | software),
				(uint8_t)(wanted[i].feature >> 8),
				(uint8_t)(wanted[i].feature & 0xff), 0x00 };
			if (hid_write(m_hidHandle, out, sizeof(out)) < 0) continue;
			for (int attempt = 0; attempt < 8; attempt++) {
				uint8_t in[64] = {0};
				const int got = hid_read_timeout(m_hidHandle, in, sizeof(in), 200);
				if (got <= 0) continue;
				if (in[2] == 0x8f) break;             // it has no such feature
				if (in[3] != out[3]) continue;        // somebody else's reply
				if (in[4] != 0x00) {                  // 0 means not present
					*wanted[i].into = in[4];
					answered = true;
				}
				break;
			}
		}
		return answered;
	#else
		return false;
	#endif
}

bool LedKeyboard::sendDataInternal(byte_buffer_t &data) {
	if (data.size() > 0) {
		#if defined(hidapi)
			if (! m_isOpen &&
			    ! open(currentDevice.vendorID, currentDevice.productID, currentDevice.serialNumber))
				return false;
			if (hid_write(m_hidHandle, const_cast<unsigned char*>(data.data()), data.size()) < 0) {
				std::cout<<"Error: Can not write to hidraw, try with the libusb version"<<std::endl;
				return false;
			}
			return true;
		#elif defined(libusb)
			if (! m_isOpen) return false;
			if (data.size() > 20) {
				if(libusb_control_transfer(m_hidHandle, 0x21, 0x09, 0x0212, 1, 
						const_cast<unsigned char*>(data.data()), data.size(), 2000) < 0)
					return false;
			} else {
				if(libusb_control_transfer(m_hidHandle, 0x21, 0x09, 0x0211, 1, 
						const_cast<unsigned char*>(data.data()), data.size(), 2000) < 0)
					return false;
			}
			usleep(1000);
			unsigned char buffer[64];
			int len = 0;
			libusb_interrupt_transfer(m_hidHandle, 0x82, buffer, sizeof(buffer), &len, 1);
			return true;
		#endif
	}
	
	return false;
}

LedKeyboard::byte_buffer_t LedKeyboard::getKeyGroupAddress(LedKeyboard::KeyAddressGroup keyAddressGroup) {
	switch (currentDevice.model) {
		case KeyboardModel::g213:
		case KeyboardModel::g413:
		  return {}; // Device doesn't support per-key setting
		case KeyboardModel::g410:
		case KeyboardModel::g512:
		case KeyboardModel::g513:
		case KeyboardModel::gpro:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0c, 0x3a, 0x00, 0x10, 0x00, 0x01 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return {};
				case LedKeyboard::KeyAddressGroup::multimedia:
					return {};
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		case KeyboardModel::g610:
		case KeyboardModel::g810:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0c, 0x3a, 0x00, 0x10, 0x00, 0x01 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return {};
				case LedKeyboard::KeyAddressGroup::multimedia:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x02, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		case KeyboardModel::g815:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, featureLighting, 0x1c };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x11, 0xff, featureLighting, 0x1c };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return { 0x11, 0xff, featureLighting, 0x1c };
				case LedKeyboard::KeyAddressGroup::multimedia:
					return { 0x11, 0xff, featureLighting, 0x1c };
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x11, 0xff, featureLighting, 0x1c };
			}
			break;
		case KeyboardModel::g910:
			switch (keyAddressGroup) {
				case LedKeyboard::KeyAddressGroup::logo:
					return { 0x11, 0xff, 0x0f, 0x3a, 0x00, 0x10, 0x00, 0x02 };
				case LedKeyboard::KeyAddressGroup::indicators:
					return { 0x12, 0xff, 0x0c, 0x3a, 0x00, 0x40, 0x00, 0x05 };
				case LedKeyboard::KeyAddressGroup::gkeys:
					return { 0x12, 0xff, 0x0f, 0x3e, 0x00, 0x04, 0x00, 0x09 };
				case LedKeyboard::KeyAddressGroup::multimedia:
					return {};
				case LedKeyboard::KeyAddressGroup::keys:
					return { 0x12, 0xff, 0x0f, 0x3d, 0x00, 0x01, 0x00, 0x0e };
			}
			break;
		default:
			break;
	}
	return {};
}
