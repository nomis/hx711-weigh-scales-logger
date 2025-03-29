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

enum Type : uint8_t {
    READING = 0,
    TARE = 1,
};

struct Data {
    uint32_t time_us;
    uint32_t type:8;
    uint32_t value:24;
};

class MemoryDeleter {
public:
        void operator()(Data *data) { ::free(data); }
};

using MemoryAllocation = std::unique_ptr<Data, MemoryDeleter>;

class HX711 {
public:
    static constexpr unsigned long BUFFER_SIZE = 80 * 900;

	HX711(int data_pin, int sck_pin);

	void init();
	void loop();

    int32_t reading();

    void start();
    void tare();
    inline bool running() const { return running_; }
    inline uint64_t start_us() const { return start_us_; }
    uint64_t duration_us() const;
    inline unsigned long count() const { return buffer_pos_; }
    inline unsigned long max_count() const { return BUFFER_SIZE; }
    void stop();

protected:
    static uuid::log::Logger logger_;

private:
    const int data_pin_;
    const int sck_pin_;
    int32_t reading_{0};
    int32_t tare_value_{0};
    uint64_t start_us_{0};
    uint64_t stop_us_{0};
    MemoryAllocation buffer_;
    unsigned long buffer_pos_{0};
    bool running_{false};
    bool tare_{false};
};

} // namespace scales
