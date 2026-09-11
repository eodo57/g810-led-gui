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

#include "ProfileParser.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

#include "../src/helpers/utils.h"

static std::string colorHex(const LedKeyboard::Color &color) {
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x",
		color.red, color.green, color.blue);
	return std::string(buffer);
}

bool parseProfileText(const std::string &text,
                      std::vector<ProfileCommand> &commands,
                      std::string &error,
                      std::vector<std::string> *allErrors) {
	std::map<std::string, std::string> vars;
	bool ok = true;
	int lineNumber = 0;

	std::istringstream stream(text);
	std::string line;
	while (std::getline(stream, line)) {
		lineNumber++;
		if (line.empty() || line[0] == '#')
			continue;

		// Tokenize like the CLI: split on single spaces (consecutive
		// spaces yield empty tokens, and inline "# comment" text simply
		// becomes extra tokens that the commands ignore).
		std::vector<std::string> args;
		while (!line.empty()) {
			size_t ind = line.find(" ");
			std::string token = line.substr(0, ind);
			if (!token.empty() && token[0] == '$')
				token = vars.count(token.substr(1)) ? vars[token.substr(1)] : "";
			args.push_back(token);
			if (ind == std::string::npos)
				break;
			line = line.substr(ind + 1);
		}
		if (args.empty())
			continue;

		auto fail = [&](const std::string &message) {
			const std::string full =
				"line " + std::to_string(lineNumber) + ": " + message;
			if (ok)
				error = full;
			if (allErrors)
				allErrors->push_back(full);
			ok = false;
		};

		ProfileCommand command;
		if (args[0] == "var") {
			if (args.size() > 2)
				vars[args[1]] = args[2];
		} else if (args[0] == "c") {
			command.type = ProfileCommand::Type::commit;
			commands.push_back(command);
		} else if (args[0] == "a" && args.size() > 1) {
			command.type = ProfileCommand::Type::all;
			if (!utils::parseColor(args[1], command.color))
				fail("invalid color '" + args[1] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "g" && args.size() > 2) {
			command.type = ProfileCommand::Type::group;
			if (!utils::parseKeyGroup(args[1], command.group))
				fail("invalid key group '" + args[1] + "'");
			else if (!utils::parseColor(args[2], command.color))
				fail("invalid color '" + args[2] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "k" && args.size() > 2) {
			command.type = ProfileCommand::Type::key;
			if (!utils::parseKey(args[1], command.key))
				fail("invalid key '" + args[1] + "'");
			else if (!utils::parseColor(args[2], command.color))
				fail("invalid color '" + args[2] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "r" && args.size() > 2) {
			command.type = ProfileCommand::Type::region;
			if (!utils::parseUInt8(args[1], command.region))
				fail("invalid region '" + args[1] + "'");
			else if (!utils::parseColor(args[2], command.color))
				fail("invalid color '" + args[2] + "'");
			else
				commands.push_back(command);
		} else if ((args[0] == "mr" || args[0] == "mn" ||
		            args[0] == "gkm") && args.size() > 1) {
			command.type = args[0] == "mr" ? ProfileCommand::Type::mr :
				args[0] == "mn" ? ProfileCommand::Type::mn :
				ProfileCommand::Type::gkm;
			if (!utils::parseUInt8(args[1], command.value))
				fail("invalid value '" + args[1] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "sm" && args.size() > 1) {
			command.type = ProfileCommand::Type::startupMode;
			if (!utils::parseStartupMode(args[1], command.startupMode))
				fail("invalid startup mode '" + args[1] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "obm" && args.size() > 1) {
			command.type = ProfileCommand::Type::onBoardMode;
			if (!utils::parseOnBoardMode(args[1], command.onBoardMode))
				fail("invalid on-board mode '" + args[1] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "rain" && args.size() > 1) {
			command.type = ProfileCommand::Type::rain;
			if (args[1] == "off") {
				command.value = 0;
				commands.push_back(command);
			} else if (!utils::parseColor(args[1], command.color)) {
				fail("invalid rain color '" + args[1] + "'");
			} else {
				command.value = 1;
				command.rainOverlay = args.size() > 2 && args[2] == "overlay";
				commands.push_back(command);
			}
		} else if (args[0] == "wave" && args.size() > 5) {
			command.type = ProfileCommand::Type::wave;
			command.waveTravel = args[1] == "travel";
			if (args[2] == "triangle") command.waveShape = 1;
			else if (args[2] == "square") command.waveShape = 2;
			else if (args[2] == "saw") command.waveShape = 3;
			else command.waveShape = 0;
			unsigned period = (unsigned)std::strtoul(args[3].c_str(), NULL, 10);
			command.period = std::chrono::duration<uint16_t, std::milli>(
				(uint16_t)std::min(period, 65000u));
			command.waveMin = (uint8_t)std::min(100ul, std::strtoul(args[4].c_str(), NULL, 10));
			command.waveMax = (uint8_t)std::min(100ul, std::strtoul(args[5].c_str(), NULL, 10));
			commands.push_back(command);
		} else if (args[0] == "audio" && args.size() > 1) {
			command.type = ProfileCommand::Type::audio;
			if (args[1] == "off") {
				command.value = 0;
				commands.push_back(command);
			} else if (args.size() < 7) {
				fail("audio needs mode, two colors, gain, smoothing and "
				     "auto|fixed");
			} else {
				command.value = 1;
				command.audioMode = args[1] == "rainbow" ? 1 :
					args[1] == "level" ? 2 : args[1] == "beat" ? 3 : 0;
				if (!utils::parseColor(args[2], command.color))
					fail("invalid audio color '" + args[2] + "'");
				else if (!utils::parseColor(args[3], command.audioHighColor))
					fail("invalid audio color '" + args[3] + "'");
				else {
					command.audioGain = (uint16_t)std::min(2000ul,
						std::strtoul(args[4].c_str(), NULL, 10));
					command.audioSmoothing = (uint8_t)std::min(95ul,
						std::strtoul(args[5].c_str(), NULL, 10));
					command.audioAutoGain = args[6] != "fixed";
					// Optional overlay token, then the source name as the
					// rest of the line: it is the only free-form argument
					// in the format, so it has to come last.
					size_t first = 7;
					if (args.size() > 7 &&
					    (args[7] == "overlay" || args[7] == "solid")) {
						command.audioOverlay = args[7] == "overlay";
						first = 8;
					}
					for (size_t i = first; i < args.size(); ++i) {
						if (i > first)
							command.audioSource += " ";
						command.audioSource += args[i];
					}
					commands.push_back(command);
				}
			}
		} else if (args[0] == "screen" && args.size() > 1) {
			command.type = ProfileCommand::Type::screen;
			if (args[1] == "off") {
				command.value = 0;
				commands.push_back(command);
			} else if (args.size() < 4) {
				fail("screen needs a mode, a boost and a smoothing");
			} else {
				command.value = 1;
				command.screenMode = args[1] == "dominant" ? 1 : 0;
				command.screenBoost = (uint8_t)std::min(100ul,
					std::strtoul(args[2].c_str(), NULL, 10));
				command.screenSmoothing = (uint8_t)std::min(95ul,
					std::strtoul(args[3].c_str(), NULL, 10));
				if (args.size() > 4)
					command.screenOverlay = args[4] == "overlay";
				// No display identifier on purpose: which monitor this
				// was means nothing on another machine, and the desktop
				// remembers the local choice itself.
				commands.push_back(command);
			}
		} else if (args[0] == "wp" && args.size() > 2) {
			command.type = ProfileCommand::Type::wavePoint;
			command.waveT = (float)std::strtod(args[1].c_str(), NULL);
			command.waveY = (float)std::strtod(args[2].c_str(), NULL);
			commands.push_back(command);
		} else if (args[0] == "wc" && args.size() > 2) {
			command.type = ProfileCommand::Type::waveColor;
			command.waveT = (float)std::strtod(args[1].c_str(), NULL);
			if (!utils::parseColor(args[2], command.color))
				fail("invalid wave color '" + args[2] + "'");
			else
				commands.push_back(command);
		} else if (args[0] == "fx" && args.size() > 2) {
			// fx argument roles mirror the CLI's setFX: color/breathing
			// take a color (breathing also a period), the wave family
			// takes only a period.
			command.type = ProfileCommand::Type::fx;
			if (!utils::parseNativeEffect(args[1], command.effect))
				fail("invalid effect '" + args[1] + "'");
			else if (!utils::parseNativeEffectPart(args[2], command.part))
				fail("invalid target '" + args[2] + "'");
			else if (args.size() < 4 &&
			         command.effect != LedKeyboard::NativeEffect::off) {
				// Only "off" takes no third argument; every other effect
				// indexes args[3] below.
				fail("effect '" + args[1] + "' needs a color or a period");
			} else {
				bool lineOk = true;
				switch (command.effect) {
					case LedKeyboard::NativeEffect::color:
						if (!utils::parseColor(args[3], command.color)) {
							fail("invalid color '" + args[3] + "'");
							lineOk = false;
						}
						break;
					case LedKeyboard::NativeEffect::breathing:
						if (!utils::parseColor(args[3], command.color)) {
							fail("invalid color '" + args[3] + "'");
							lineOk = false;
						} else if (args.size() < 5 ||
						           !utils::parsePeriod(args[4], command.period)) {
							fail("invalid period");
							lineOk = false;
						}
						break;
					case LedKeyboard::NativeEffect::off:
						// no color or period required
						break;
					case LedKeyboard::NativeEffect::ripple:
						if (!utils::parsePeriod(args[3], command.period)) {
							fail("invalid period '" + args[3] + "'");
							lineOk = false;
						}
						break;
					default:  // cycle, waves, hwave, vwave, cwave
						if (!utils::parsePeriod(args[3], command.period)) {
							fail("invalid period '" + args[3] + "'");
							lineOk = false;
						}
						break;
				}
				if (lineOk)
					commands.push_back(command);
			}
		}
		// Unknown commands are ignored, like the CLI does.
	}
	return ok;
}

std::string serializeProfileText(const std::vector<ProfileCommand> &commands) {
	std::ostringstream out;
	out << "# g810-led profile\n";
	for (const ProfileCommand &command : commands) {
		switch (command.type) {
			case ProfileCommand::Type::all:
				out << "a " << colorHex(command.color) << "\n";
				break;
			case ProfileCommand::Type::key: {
				std::string name = utils::keyName(command.key);
				if (name.empty())
					break;
				out << "k " << name << " " << colorHex(command.color) << "\n";
				break;
			}
			case ProfileCommand::Type::region: {
				char region[4];
				std::snprintf(region, sizeof(region), "%02x", command.region);
				out << "r " << region << " " << colorHex(command.color) << "\n";
				break;
			}
			case ProfileCommand::Type::commit:
				out << "c\n";
				break;
			case ProfileCommand::Type::wave: {
				const char *mode = command.waveTravel ? "travel" : "pulse";
				const char *shape = "sine";
				if (command.waveShape == 1) shape = "triangle";
				else if (command.waveShape == 2) shape = "square";
				else if (command.waveShape == 3) shape = "saw";
				out << "wave " << mode << " " << shape << " "
				    << command.period.count() << " "
				    << (int)command.waveMin << " " << (int)command.waveMax << "\n";
				break;
			}
			case ProfileCommand::Type::wavePoint:
				out << "wp " << command.waveT << " " << command.waveY << "\n";
				break;
			case ProfileCommand::Type::waveColor:
				out << "wc " << command.waveT << " " << colorHex(command.color) << "\n";
				break;
			case ProfileCommand::Type::audio:
				if (command.value == 0)
					out << "audio off\n";
				else {
					const char *mode = command.audioMode == 1 ? "rainbow" :
						command.audioMode == 2 ? "level" :
						command.audioMode == 3 ? "beat" : "bars";
					out << "audio " << mode << " " << colorHex(command.color)
					    << " " << colorHex(command.audioHighColor)
					    << " " << (int)command.audioGain
					    << " " << (int)command.audioSmoothing
					    << " " << (command.audioAutoGain ? "auto" : "fixed")
					    << " " << (command.audioOverlay ? "overlay" : "solid");
					if (!command.audioSource.empty())
						out << " " << command.audioSource;
					out << "\n";
				}
				break;
			case ProfileCommand::Type::screen:
				if (command.value == 0)
					out << "screen off\n";
				else
					out << "screen "
					    << (command.screenMode == 1 ? "dominant" : "mirror")
					    << " " << (int)command.screenBoost
					    << " " << (int)command.screenSmoothing
					    << " " << (command.screenOverlay ? "overlay" : "solid")
					    << "\n";
				break;
			case ProfileCommand::Type::rain:
				if (command.value == 0)
					out << "rain off\n";
				else {
					out << "rain " << colorHex(command.color);
					if (command.rainOverlay)
						out << " overlay";
					out << "\n";
				}
				break;
			case ProfileCommand::Type::fx: {
				std::string ename = "color";
				switch (command.effect) {
					case LedKeyboard::NativeEffect::color: ename = "color"; break;
					case LedKeyboard::NativeEffect::breathing: ename = "breathing"; break;
					case LedKeyboard::NativeEffect::cycle: ename = "cycle"; break;
					case LedKeyboard::NativeEffect::waves: ename = "waves"; break;
					case LedKeyboard::NativeEffect::hwave: ename = "hwave"; break;
					case LedKeyboard::NativeEffect::vwave: ename = "vwave"; break;
					case LedKeyboard::NativeEffect::cwave: ename = "cwave"; break;
					case LedKeyboard::NativeEffect::off: ename = "off"; break;
					case LedKeyboard::NativeEffect::ripple: ename = "ripple"; break;
					default: break;
				}
				std::string pname = "all";
				if (command.part == LedKeyboard::NativeEffectPart::keys) pname = "keys";
				else if (command.part == LedKeyboard::NativeEffectPart::logo) pname = "logo";
				out << "fx " << ename << " " << pname;
				bool needsColor = (command.effect == LedKeyboard::NativeEffect::color || command.effect == LedKeyboard::NativeEffect::breathing);
				if (needsColor) {
					out << " " << colorHex(command.color);
				}
				bool needsPeriod = (command.effect == LedKeyboard::NativeEffect::breathing ||
				                    command.effect == LedKeyboard::NativeEffect::ripple ||
				                    (command.effect != LedKeyboard::NativeEffect::color &&
				                     command.effect != LedKeyboard::NativeEffect::off));
				// parsePeriod (GUI and CLI) reads "<n>ms"/"<n>s" or a two-digit
				// hex byte — a bare decimal is not a period, so the unit
				// suffix is what makes the line reloadable.
				if (needsPeriod && command.period.count() > 0) {
					out << " " << command.period.count() << "ms";
				}
				out << "\n";
				break;
			}
			case ProfileCommand::Type::mr:
				out << "mr " << (int)command.value << "\n";
				break;
			case ProfileCommand::Type::mn:
				out << "mn " << (int)command.value << "\n";
				break;
			case ProfileCommand::Type::gkm:
				out << "gkm " << (int)command.value << "\n";
				break;
			case ProfileCommand::Type::startupMode:
				out << "sm " << (command.startupMode == LedKeyboard::StartupMode::color ? "color" : "wave") << "\n";
				break;
			case ProfileCommand::Type::onBoardMode:
				out << "obm " << (command.onBoardMode == LedKeyboard::OnBoardMode::software ? "software" : "board") << "\n";
				break;
			default:
				break;
		}
	}
	return out.str();
}
