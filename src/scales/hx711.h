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

#include <uuid/log.h>

namespace scales {

class HX711 {
public:
	HX711(int data_pin, int sck_pin);

	void start();
	void loop();

protected:
    static uuid::log::Logger logger_;

private:
    const int data_pin_;
    const int sck_pin_;
};

} // namespace scales
