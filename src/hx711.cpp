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
#include <mutex>
#include <string>
#include <string_view>
#include <sys/time.h>
#include <vector>

#include <CBOR.h>
#include <CBOR_parsing.h>
#include <CBOR_streams.h>

#include <uuid/log.h>

#include "app/app.h"
#include "app/fs.h"
#include "app/util.h"

namespace cbor = qindesign::cbor;
using app::FS;

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

	std::lock_guard lock{mutex_};

	if (running_ && buffer_pos_ < BUFFER_SIZE) {
		Data &data = buffer_.get()[buffer_pos_++];

		data.time_us = std::min(now - start_us_, (uint64_t)UINT32_MAX);
		data.type = tare_ ? Type::TARE : Type::READING;
		data.value = value;

		logger_.trace("Reading: %d (%07x) [%lu]", value, reading, buffer_pos_);

		if (tare_) {
			buffer_tare_ = true;
		}

		if (buffer_pos_ == BUFFER_SIZE) {
			logger_.notice("Maximum readings reached");
		}
	} else {
		logger_.trace("Reading: %d (%07x)", value, reading);
	}

	reading_ = value;
	if (tare_) {
		logger_.info("Tare: %d", value);
		tare_value_ = value;
		tare_ = false;
	}
}

int32_t HX711::reading() {
	std::lock_guard lock{mutex_};

	return reading_ - tare_value_;
}

void HX711::start() {
	std::lock_guard lock{mutex_};

	gettimeofday(&realtime_us_, NULL);
	start_us_ = 0;
	stop_us_ = 0;
	buffer_pos_ = 0;
	running_ = false;

	if (realtime_us_.tv_sec < 0 || (unsigned long)realtime_us_.tv_sec < EPOCH_S) {
		return;
	}

	logger_.info("Start");
	start_us_ = ::esp_timer_get_time();
	buffer_pos_ = 0;
	buffer_tare_ = false;
	running_ = true;
	tare_ = false;
}

void HX711::tare() {
	std::lock_guard lock{mutex_};
	tare_ = true;
}

uint64_t HX711::duration_us() const {
	std::lock_guard lock{mutex_};

	if (running_) {
		return ::esp_timer_get_time() - start_us_;
	} else {
		return stop_us_ - start_us_;
	}
}

void HX711::stop() {
	std::lock_guard lock{mutex_};

	if (running_) {
		stop_us_ = ::esp_timer_get_time();
		logger_.info("Stop");
		save();
	}
	running_ = false;
}

void HX711::save() {
	std::string filename;

	filename.append(DIRECTORY_NAME);
	filename.append("/");
	filename.append(std::to_string(realtime_us_.tv_sec));
	filename.append(FILENAME_EXT);

	std::lock_guard lock{app::App::file_mutex()};

	auto file = FS.open(filename.c_str(), "w", true);
	if (!file) {
		logger_.err(F("Unable to open file %s for writing"), filename.c_str());
		return;
	}

	logger_.info(F("Writing %s"), filename.c_str());

	cbor::Writer writer{file};

	writer.writeTag(cbor::kSelfDescribeTag);
	writer.beginMap(5);

	app::write_text(writer, "realtime_s_us");
	writer.beginArray(2);
	writer.writeUnsignedInt(realtime_us_.tv_sec);
	writer.writeUnsignedInt(realtime_us_.tv_usec);

	app::write_text(writer, "start_us");
	writer.writeUnsignedInt(start_us_);

	app::write_text(writer, "stop_us");
	writer.writeUnsignedInt(stop_us_);

	app::write_text(writer, "readings_format");
	writer.beginArray(3);
	app::write_text(writer, "[flags:text]");
	app::write_text(writer, "<offset_time_us:uint>");
	app::write_text(writer, "<offset_value:int>");

	app::write_text(writer, "readings");
	writer.beginIndefiniteArray();

	uint32_t previous_us = 0;
	int32_t previous_value = 0;

	for (unsigned long i = 0; i < buffer_pos_; i++) {
		const Data &data = buffer_.get()[i];
		int32_t value = ((data.value & 0x800000) ? 0xFF000000 : 0) | data.value;

		if (data.type == Type::TARE) {
			app::write_text(writer, "tare");
		}

		writer.writeUnsignedInt(data.time_us - previous_us);
		previous_us = data.time_us;
		writer.writeInt(value - previous_value);
		previous_value = value;
	}

	writer.endIndefinite();

	if (file.getWriteError()) {
		logger_.err(F("Failed to write file %s: %u"), filename.c_str(), file.getWriteError());
		file.close();
		FS.remove(filename.c_str());
	}

	logger_.info(F("Saved readings to %s"), filename.c_str());
}

void HX711::list_files(std::function<void(const std::string &filename,
		const std::string &timestamp)> func) {
	std::lock_guard lock{app::App::file_mutex()};
	const char mode[2] = { 'r', '\0' };
	auto dir = FS.open(DIRECTORY_NAME, mode);
	size_t len = strlen(DIRECTORY_NAME) + 1;

	while (true) {
		auto name = dir.getNextFileName();
		if (name.length() > len) {
			std::string filename{name.c_str() + len};

			func(filename, file_name(filename, false));
		} else {
			break;
		}
	}
}

bool HX711::file_exists(const std::string_view filename) {
	std::lock_guard lock{app::App::file_mutex()};
	std::string path = DIRECTORY_NAME;

	path.append("/");
	path.append(filename);

	return !filename.empty() && FS.open(path.c_str());
}

std::string HX711::file_name(const std::string &filename, bool safe) {
	std::string timestamp;
	struct tm tm;
	time_t t = ::atoi(filename.c_str());

	tm.tm_year = 0;
	::gmtime_r(&t, &tm);

	if (tm.tm_year != 0) {
		std::vector<char> tmp(48);

		::snprintf(tmp.data(), tmp.size(),
			safe ? "%04u-%02u-%02u_%02u-%02u-%02u" :
			"%04u-%02u-%02u %02u:%02u:%02u",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		return {tmp.data()};
	} else {
		return filename;
	}
}

void HX711::get_file(const std::string_view filename, Stream &output) {
	std::lock_guard lock{app::App::file_mutex()};
	std::string path = DIRECTORY_NAME;

	path.append("/");
	path.append(filename);

	auto file = FS.open(path.c_str());

	if (file) {
		std::vector<char> buf(512);

		while (true) {
			size_t len = file.readBytes(buf.data(), buf.size());

			if (len > 0) {
				output.write(buf.data(), len);
			} else {
				break;
			}
		}
	}
}

void HX711::delete_file(const std::string_view filename) {
	std::lock_guard lock{app::App::file_mutex()};
	std::string path = DIRECTORY_NAME;

	path.append("/");
	path.append(filename);

	FS.remove(path.c_str());
}

} // namespace scales
