/*
 * hx711-weigh-scales-logger - HX711 weigh scales data logger
 * Copyright 2025  Simon Arlott
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "scales/console.h"

#include <memory>
#include <string>
#include <vector>

#include <uuid/console.h>
#include <uuid/log.h>

#include "scales/app.h"
#include "app/config.h"
#include "app/console.h"

using ::uuid::flash_string_vector;
using ::uuid::console::Commands;
using ::uuid::console::Shell;
using LogLevel = ::uuid::log::Level;
using LogFacility = ::uuid::log::Facility;

using ::app::AppShell;
using ::app::CommandFlags;
using ::app::Config;
using ::app::ShellContext;

#define MAKE_PSTR(string_name, string_literal) static const char __pstr__##string_name[] __attribute__((__aligned__(sizeof(int)))) PROGMEM = string_literal;
#define MAKE_PSTR_WORD(string_name) MAKE_PSTR(string_name, #string_name)
#define F_(string_name) FPSTR(__pstr__##string_name)

MAKE_PSTR_WORD(readings)
MAKE_PSTR_WORD(start)
MAKE_PSTR_WORD(stop)
MAKE_PSTR_WORD(tare)

namespace scales {

#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wunused-const-variable"
#pragma GCC diagnostic pop

static constexpr inline AppShell &to_app_shell(Shell &shell) {
	return static_cast<AppShell&>(shell);
}

static constexpr inline App &to_app(Shell &shell) {
	return static_cast<App&>(to_app_shell(shell).app_);
}

static constexpr inline ScalesShell &to_shell(Shell &shell) {
	return static_cast<ScalesShell&>(shell);
}

#define NO_ARGUMENTS std::vector<std::string>{}

static void start(Shell &shell, const std::vector<std::string> &arguments) {
	to_app(shell).hx711().start();
}

static void tare(Shell &shell, const std::vector<std::string> &arguments) {
	to_app(shell).hx711().tare();
}

static void readings(Shell &shell, const std::vector<std::string> &arguments) {
	HX711 &hx711 = to_app(shell).hx711();

	shell.printfln(F("Current: %d"), (int)hx711.reading());

	if (hx711.start_us() > 0) {
		shell.printfln(F("Started at %" PRIu64 " (%lu.%06lu)"), hx711.start_us(),
			hx711.realtime_us().tv_sec, hx711.realtime_us().tv_usec);
		if (hx711.running()) {
			shell.printfln(F("Running for %" PRIu64), hx711.duration_us());
		} else {
			shell.printfln(F("Stopped after %" PRIu64), hx711.duration_us());
		}
		shell.printfln(F("Readings: %lu/%lu"), hx711.count(), hx711.max_count());
	} else {
		shell.printfln(F("Never started"));
	}
}

static void stop(Shell &shell, const std::vector<std::string> &arguments) {
	to_app(shell).hx711().stop();
}

static inline void setup_commands(std::shared_ptr<Commands> &commands) {
	commands->add_command({F_(start)}, start);
	commands->add_command({F_(tare)}, tare);
	commands->add_command({F_(readings)}, readings);
	commands->add_command({F_(stop)}, stop);
}

ScalesShell::ScalesShell(app::App &app, Stream &stream, unsigned int context, unsigned int flags)
	: AppShell(app, stream, context, flags) {

}

} // namespace scales

namespace app {

void setup_commands(std::shared_ptr<Commands> &commands) {
	scales::setup_commands(commands);
}

} // namespace app
