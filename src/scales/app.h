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

#pragma once

#include <Arduino.h>

#include "app/app.h"
#include "hx711.h"

namespace scales {

class App: public app::App {
private:
#if defined(ARDUINO_LOLIN_S3)
	static constexpr int LED_PIN = 38;

	static constexpr int DATA_PIN = 1;
	static constexpr int SCK_PIN = 2;
#else
# error "Unknown board"
#endif

public:
	App();

	void start() override;
	void loop() override;

	HX711& hx711() { return hx711_; }

private:
	HX711 hx711_{DATA_PIN, SCK_PIN};
};

} // namespace scales
