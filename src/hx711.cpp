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

#include "scales/hx711.h"

#include <Arduino.h>

static const char __pstr__logger_name[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM = "hx711";

namespace scales {

uuid::log::Logger HX711::logger_{FPSTR(__pstr__logger_name), uuid::log::Facility::DAEMON};

HX711::HX711(int data_pin, int sck_pin)
		: data_pin_(data_pin), sck_pin_(sck_pin) {
}

void HX711::start() {
	pinMode(sck_pin_, OUTPUT);
	digitalWrite(sck_pin_, LOW);
	pinMode(data_pin_, INPUT_PULLUP);

	digitalWrite(sck_pin_, HIGH);
	delayMicroseconds(100);
	digitalWrite(sck_pin_, LOW);
}

void HX711::loop() {
	if (digitalRead(data_pin_) == HIGH)
		return;

	uint32_t data = 0;

	delayMicroseconds(1); // T1

	noInterrupts();
	for (int i = 0; i < 25; i++) {
		digitalWrite(sck_pin_, HIGH);
		delayMicroseconds(1); // T2 & T3
		data |= digitalRead(data_pin_) == HIGH ? 1 : 0;
		data <<= 1;
		digitalWrite(sck_pin_, LOW);
		delayMicroseconds(1); // T4
	}
	interrupts();

	data >>= 1;

	if ((data & 1) != 1)
		return;

	int32_t value = ((data & 0x1000000) ? 0xFF000000 : 0) | (data >> 1);

	logger_.trace("Reading: %d (%07x)", value, data);
}

} // namespace scales
