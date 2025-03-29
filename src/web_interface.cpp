/*
 * hx711-weigh-scales-logger - HX711 weigh scales data logger
 * Copyright 2023-2025  Simon Arlott
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

#include "scales/web_interface.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <time.h>
#include <vector>

#include "app/config.h"
#include "scales/app.h"
#include "scales/web_server.h"
#include "htdocs/status.xml.gz.h"

#ifndef PSTR_ALIGN
# define PSTR_ALIGN 4
#endif

using uuid::log::format_timestamp_ms;

static const char __pstr__logger_name[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM = "web-interface";

namespace scales {

uuid::log::Logger WebInterface::logger_{FPSTR(__pstr__logger_name), uuid::log::Facility::DAEMON};

static const char * const gzip_immutable_headers[][2] = {
	{ "Content-Encoding", "gzip" },
	{ "Cache-Control", "public, immutable, max-age=31536000" },
	{ nullptr, nullptr }
};

WebInterface::WebInterface(App &app) : app_(app) {
	using namespace std::placeholders;

	server_.add_get_handler("/", std::bind(&WebInterface::status, this, _1));
	server_.add_post_handler("/action", std::bind(&WebInterface::action, this, _1));
	server_.add_static_content("/" + app_.immutable_id() + "/status.xml",
		"application/xslt+xml", gzip_immutable_headers, htdocs_status_xml_gz);
}

bool WebInterface::status(WebServer::Request &req) {
	req.set_status(200);
	req.set_type("application/xml");
	req.add_header("Cache-Control", "no-cache");

	req.printf(
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
			"<?xml-stylesheet type=\"text/xsl\" href=\"/%s/status.xml\"?>"
			"<r>", app_.immutable_id().c_str()
	);

	HX711 &hx711 = app_.hx711();

	req.printf("<v>%d</v>", (int)hx711.reading());

	if (hx711.start_us() > 0) {
		std::vector<char> realtime(32);
		time_t t = hx711.realtime_us().tv_sec;
		struct tm tm{};

		::localtime_r(&t, &tm);
		::strftime(realtime.data(), realtime.size(), "%F %H:%M:%S", &tm);

		req.printf("<s t=\"%s\" u=\"%s\" d=\"%s\" c=\"%lu\" m=\"%lu\">",
			realtime.data(),
			format_timestamp_ms(hx711.start_us() / 1000).c_str(),
			format_timestamp_ms(hx711.duration_us() / 1000).c_str(),
			hx711.count(), hx711.max_count());

		if (hx711.running()) {
			req.printf("<a/>");
		}

		if (hx711.has_tare()) {
			req.printf("<z/>");
		}

		req.printf("</s>");
	} else {
		req.printf("<n/>");
	}

	req.print("</r>");
	return true;
}

bool WebInterface::action(WebServer::Request &req) {
	if (req.get_header("Content-Type") != "application/x-www-form-urlencoded") {
		req.set_status(400);
		return true;
	}

	size_t len = req.available();

	if (len > 256) {
		req.set_status(413);
		return true;
	}

	std::vector<char> buffer(len);

	req.readBytes(buffer.data(), buffer.size());

	std::string_view text{buffer.data(), buffer.size()};
	auto params = parse_form(text);
	std::string_view action;
	const char *message = nullptr;

	auto it = params.find("action");
	if (it != params.end())
		action = it->second;

	HX711 &hx711 = app_.hx711();
	std::function func = []{};

	if (action == "start") {
		message = "Started";
		func = [&]{ hx711.start(); };
	} else if (action == "tare") {
		message = "Tare";
		func = [&]{ hx711.tare(); };
	} else if (action == "stop") {
		message = "Stopped";
		func = [&]{ hx711.stop(); };
	}

	if (message) {
		logger_.info("Action \"%.*s\" by %s",
			static_cast<int>(action.size()), action.begin(),
			req.client_address().c_str());
		func();
	} else {
		message = "Unknown action";
	}

	req.add_header("Cache-Control", "no-cache");
	if (!message) {
		req.set_status(303);
		req.set_type("text/plain");
		req.add_header("Location", "/");
	} else {
		req.set_status(200);
		req.set_type("text/html");
		req.printf(
			"<!DOCTYPE html><html><head>"
			"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
			"<meta http-equiv=\"refresh\" content=\"0;URL=/\">"
			"<link rel=\"icon\" href=\"data:,\"/>"
			"</head><body><p>%s</p></body></html>", message);
	}
	return true;
}

std::unordered_map<std::string_view,std::string_view>
		WebInterface::parse_form(std::string_view text) {
	std::unordered_map<std::string_view,std::string_view> params;

	while (text.length() > 0) {
		std::string_view value;
		auto amp_pos = text.find('&');

		if (amp_pos != std::string_view::npos) {
			value = text.substr(0, amp_pos);
			text.remove_prefix(amp_pos + 1);
		} else {
			value = text;
			text = {};
		}

		auto eq_pos = value.find('=');

		if (eq_pos != std::string_view::npos) {
			params.emplace(value.substr(0, eq_pos), value.substr(eq_pos + 1));
		} else {
			params.emplace(value, "");
		}
	}

	return params;
}

} // namespace scales
