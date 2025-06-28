#!/usr/bin/env python3
# hx711-weigh-scales-logger - HX711 weigh scales data logger
# Copyright 2025  Simon Arlott
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import cbor2
import csv
import sys


def decode(f):
	data = cbor2.load(f)
	assert data["readings_format"] == ['[flags:text]', '<offset_time_us:uint>', '<offset_value:int>'], data["readings_format"]

	now_us = 0
	value = 0
	flags = set()

	readings = []
	offset_time_us = None

	for reading in data["readings"]:
		if isinstance(reading, str):
			flags.add(reading)
		elif offset_time_us is None:
			offset_time_us = reading
		else:
			offset_value = reading
			assert offset_time_us >= 0

			now_us += offset_time_us
			value += offset_value

			readings.append({"time_us": now_us, "value": value, "flags": flags.copy()})

			offset_time_us = None
			flags.clear()

	data["readings"] = readings
	return data


def encode_csv(data, f):
	writer = csv.writer(f, dialect="unix", quoting=csv.QUOTE_MINIMAL)
	writer.writerow(["Time (us)", "Value", "Tare"])
	for reading in data["readings"]:
		writer.writerow([reading["time_us"], reading["value"], 1 if "tare" in reading["flags"] else 0])


if __name__ == "__main__":
	for filename in sys.argv[1:]:
		if filename.endswith(".cbor"):
			with open(filename, "rb") as f:
				data = decode(f)
			with open(filename[:-4] + "csv", "wt") as f:
				encode_csv(data, f)
