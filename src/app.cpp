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

#include "scales/app.h"

#include <Arduino.h>

#include <memory>

#include "scales/web_interface.h"

namespace scales {

App::App() {
}

void App::start() {
	app::App::start();
	hx711_.init();
	web_interface_ = std::make_unique<WebInterface>(*this);
}

void App::loop() {
	app::App::loop();
	hx711_.loop();
}

} // namespace scales
