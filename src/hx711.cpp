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

#include <algorithm>
#include <cassert>

static const char __pstr__logger_name[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM = "hx711";

namespace scales {

uuid::log::Logger HX711::logger_{FPSTR(__pstr__logger_name), uuid::log::Facility::DAEMON};

HX711::HX711(int data_pin, int sck_pin)
		: data_pin_(data_pin), sck_pin_(sck_pin),
		buffer_(reinterpret_cast<Data*>(
				::heap_caps_malloc(BUFFER_SIZE * sizeof(Data),
					MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
	assert(buffer_.get());
}

void HX711::init() {
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

	uint32_t reading = 0;

	delayMicroseconds(1); // T1

	noInterrupts();
	for (int i = 0; i < 25; i++) {
		digitalWrite(sck_pin_, HIGH);
		delayMicroseconds(1); // T2 & T3
		reading |= digitalRead(data_pin_) == HIGH ? 1 : 0;
		reading <<= 1;
		digitalWrite(sck_pin_, LOW);
		delayMicroseconds(1); // T4
	}
	interrupts();

	reading >>= 1;

	if ((reading & 1) != 1)
		return;

	int32_t value = ((reading & 0x1000000) ? 0xFF000000 : 0) | (reading >> 1);
	uint64_t now = ::esp_timer_get_time();

	if (running_ && buffer_pos_ < BUFFER_SIZE) {
		Data &data = buffer_.get()[buffer_pos_++];

		data.time_us = std::min(now - start_us_, (uint64_t)UINT32_MAX);
		data.type = tare_ ? Type::TARE : Type::READING;
		data.value = value;

		logger_.trace("Reading: %d (%07x) [%lu]", value, reading, buffer_pos_);
	} else {
		logger_.trace("Reading: %d (%07x)", value, reading);
	}

	reading_ = value;
	if (tare_) {
		tare_value_ = value;
		tare_ = false;
	}
}

int32_t HX711::reading() {
	return reading_ - tare_value_;
}

void HX711::start() {
	start_us_ = ::esp_timer_get_time();
	buffer_pos_ = 0;
	running_ = true;
	tare_ = false;
}

void HX711::tare() {
	tare_ = true;
}

uint64_t HX711::duration_us() const {
	if (running_) {
		return ::esp_timer_get_time() - start_us_;
	} else {
		return stop_us_ - start_us_;
	}
}

void HX711::stop() {
	if (running_)
		stop_us_ = ::esp_timer_get_time();
	running_ = false;
}

} // namespace scales
