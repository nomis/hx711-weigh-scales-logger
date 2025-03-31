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

#include "scales/web_server.h"

#include <Arduino.h>

#include <arpa/inet.h>
#include <esp_http_server.h>
#include <sys/socket.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef PSTR_ALIGN
# define PSTR_ALIGN 4
#endif

static const char __pstr__logger_name[] __attribute__((__aligned__(PSTR_ALIGN))) PROGMEM = "web-server";

namespace scales {

uuid::log::Logger WebServer::logger_{FPSTR(__pstr__logger_name), uuid::log::Facility::DAEMON};

WebServer::WebServer(uint16_t port) {
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = nullptr;
	esp_err_t err;

	config.task_priority = uxTaskPriorityGet(nullptr);
	config.server_port = port;
	config.uri_match_fn = httpd_uri_match_wildcard;

	err = httpd_start(&server, &config);

	if (err == ESP_OK) {
		handle_ = std::unique_ptr<void,HandleDeleter>{server};
		logger_.debug("Started HTTP server");
	} else {
		logger_.crit("Failed to start HTTP server: %d", err);
	}
}

WebServer::~WebServer() {
	if (!handle_)
		return;

	for (auto &uri_handler : uri_handlers_)
		uri_handler->server_unregister(handle_.get());
}

bool WebServer::add_get_handler(const std::string &uri, get_function handler) {
	if (!handle_)
		return false;

	uri_handlers_.push_back(std::make_unique<GetURIHandler>(uri, std::move(handler)));

	if (uri_handlers_.back()->server_register(handle_.get()))
		return true;

	logger_.crit("Failed to register GET handler for URI %s", uri.c_str());

	uri_handlers_.pop_back();
	return false;
}

bool WebServer::add_post_handler(const std::string &uri, get_function handler) {
	if (!handle_)
		return false;

	uri_handlers_.push_back(std::make_unique<PostURIHandler>(uri, std::move(handler)));

	if (uri_handlers_.back()->server_register(handle_.get()))
		return true;

	logger_.crit("Failed to register POST handler for URI %s", uri.c_str());

	uri_handlers_.pop_back();
	return false;
}

bool WebServer::add_static_content(const std::string &uri, const char *content_type,
		const char * const headers[][2], const std::string_view data) {
	if (!handle_)
		return false;

	uri_handlers_.push_back(std::make_unique<StaticContentURIHandler>(uri,
		content_type, headers, data));

	if (uri_handlers_.back()->server_register(handle_.get()))
		return true;

	logger_.crit("Failed to register GET handler for URI %s", uri.c_str());

	uri_handlers_.pop_back();
	return false;
}

WebServer::URIHandler::URIHandler(const std::string &uri) : uri_(uri) {
}

bool WebServer::URIHandler::server_register(httpd_handle_t server) {
	httpd_uri_t httpd_handler;

	httpd_handler.method = method();
	httpd_handler.uri = uri_.c_str();
	httpd_handler.user_ctx = this;
	httpd_handler.handler = [] (httpd_req_t *req) -> esp_err_t {
		return reinterpret_cast<WebServer::URIHandler*>(req->user_ctx)->handler_function(req);
	};

	return httpd_register_uri_handler(server, &httpd_handler) == ESP_OK;
}

void WebServer::URIHandler::server_unregister(httpd_handle_t server) {
	httpd_unregister_uri_handler(server, uri_.c_str(), method());
}

WebServer::GetURIHandler::GetURIHandler(const std::string &uri, get_function handler)
		: URIHandler(uri), function_(handler) {
}

WebServer::PostURIHandler::PostURIHandler(const std::string &uri, post_function handler)
		: URIHandler(uri), function_(handler) {
}

WebServer::StaticContentURIHandler::StaticContentURIHandler(const std::string &uri,
		const char *content_type, const char * const headers[][2],
		const std::string_view data)
		: URIHandler(uri), content_type_(content_type), headers_(headers),
		data_(data) {
}

httpd_method_t WebServer::GetURIHandler::method() {
	return HTTP_GET;
}

esp_err_t WebServer::GetURIHandler::handler_function(httpd_req_t *req) {
	Request ws_req{req};

	if (function_(ws_req)) {
		ws_req.finish();
		return ESP_OK;
	} else {
		return ESP_FAIL;
	}
}

httpd_method_t WebServer::PostURIHandler::method() {
	return HTTP_POST;
}

esp_err_t WebServer::PostURIHandler::handler_function(httpd_req_t *req) {
	Request ws_req{req};

	if (function_(ws_req)) {
		ws_req.finish();
		return ESP_OK;
	} else {
		return ESP_FAIL;
	}
}

httpd_method_t WebServer::StaticContentURIHandler::method() {
	return HTTP_GET;
}

esp_err_t WebServer::StaticContentURIHandler::handler_function(httpd_req_t *req) {
	httpd_resp_set_status(req, HTTPD_200);
	httpd_resp_set_type(req, content_type_);

	if (headers_ != nullptr) {
		const char * const *header = headers_[0];

		while (header[0] != nullptr) {
			httpd_resp_set_hdr(req, header[0], header[1]);
			header += 2;
		}
	}

	return httpd_resp_send(req, data_.begin(), data_.length());
}

WebServer::Request::Request(httpd_req_t *req) : req_(req),
	content_len_(req_->content_len), buffer_(1436 - 7) {
}

int WebServer::Request::available() {
	return content_len_;
}

int WebServer::Request::read() {
	char buffer;

	if (content_len_ > 0 && readBytes(&buffer, 1) == 1) {
		return buffer;
	} else {
		return -1;
	}
}

int WebServer::Request::peek() {
	return -1;
}

size_t WebServer::Request::readBytes(char *buffer, size_t length) {
	esp_err_t ret = httpd_req_recv(req_, buffer, length);
	return ret < 0 ? 0 : ret;
}

size_t WebServer::Request::write(uint8_t c) {
	buffer_[buffer_len_] = (char)c;
	buffer_len_++;

	if (buffer_len_ == buffer_.size())
		send();

	return 1;
}

size_t WebServer::Request::write(const uint8_t *buffer, size_t size) {
	size_t written = 0;

	while (size > 0) {
		auto remaining = std::min(size, buffer_.size() - buffer_len_);

		std::memcpy(&buffer_[buffer_len_], buffer + written, remaining);
		buffer_len_ += remaining;

		size -= remaining;
		written += remaining;

		if (buffer_len_ == buffer_.size())
			send();
	}

	return written;
}

void WebServer::Request::send() {
	if (buffer_len_ > 0) {
		if (send_err_ == ESP_OK)
			send_err_ = httpd_resp_send_chunk(req_, buffer_.data(), buffer_len_);
		buffer_len_ = 0;
		sent_ = true;
	}
}

void WebServer::Request::finish() {
	if (sent_) {
		send();
		httpd_resp_send_chunk(req_, nullptr, 0);
	} else {
		if (!status_)
			httpd_resp_set_status(req_, HTTPD_204);

		httpd_resp_send(req_, buffer_.data(), buffer_len_);
	}
}

const std::string_view WebServer::Request::uri() const {
	return req_->uri;
}

void WebServer::Request::set_status(unsigned int status) {
	if (status == 200) {
		httpd_resp_set_status(req_, HTTPD_200);
	} else if (status == 303) {
		httpd_resp_set_status(req_, "303 See Other");
	} else if (status == 400) {
		httpd_resp_set_status(req_, HTTPD_400);
	} else if (status == 404) {
		httpd_resp_set_status(req_, HTTPD_404);
	} else if (status == 413) {
		httpd_resp_set_status(req_, "413 Request Entity Too Large");
	} else {
		httpd_resp_set_status(req_, HTTPD_500);
	}
	status_ = true;
}

void WebServer::Request::set_type(const char *type) {
	httpd_resp_set_type(req_, type);
}

void WebServer::Request::add_header(const char *name, const char *value) {
	httpd_resp_set_hdr(req_, name, value);
}

void WebServer::Request::add_header(const char *name, const std::string &value) {
	resp_headers_.emplace_back(strdup(value.c_str()));
	add_header(name, resp_headers_.back().get());
}

std::string WebServer::Request::client_address() {
	struct sockaddr_storage addr{};
	char ip[INET6_ADDRSTRLEN] = { 0 };

	int fd = httpd_req_to_sockfd(req_);
	socklen_t addrlen = sizeof(addr);

	if (::getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen)) {
		return "[unknown PN]";
	}

	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *sa = reinterpret_cast<struct sockaddr_in*>(&addr);

		if (::inet_ntop(sa->sin_family, &sa->sin_addr, ip, sizeof(ip)) == nullptr) {
			return "[unknown V4]";
		}

		return std::string{"["} + ip + "]:" + std::to_string(ntohs(sa->sin_port));
	} else if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *sa = reinterpret_cast<struct sockaddr_in6*>(&addr);

		if (::inet_ntop(sa->sin6_family, &sa->sin6_addr, ip, sizeof(ip)) == nullptr) {
			return "[unknown V6]";
		}

		if (sa->sin6_addr.s6_addr[0] == 0
				&& sa->sin6_addr.s6_addr[1] == 0
				&& sa->sin6_addr.s6_addr[2] == 0
				&& sa->sin6_addr.s6_addr[3] == 0
				&& sa->sin6_addr.s6_addr[4] == 0
				&& sa->sin6_addr.s6_addr[5] == 0
				&& sa->sin6_addr.s6_addr[6] == 0
				&& sa->sin6_addr.s6_addr[7] == 0
				&& sa->sin6_addr.s6_addr[8] == 0
				&& sa->sin6_addr.s6_addr[9] == 0
				&& sa->sin6_addr.s6_addr[10] == 0xFF
				&& sa->sin6_addr.s6_addr[11] == 0xFF
				&& !strncasecmp("::FFFF:", ip, 7)) {
			std::memmove(ip, &ip[7], sizeof(ip) - 7);
		}

		return std::string{"["} + ip + "]:" + std::to_string(ntohs(sa->sin6_port));
	} else {
		return "[unknown AF]";
	}
}

std::string WebServer::Request::get_header(const char *name) {
	size_t len = httpd_req_get_hdr_value_len(req_, name);

	if (len == 0)
		return "";

	std::vector<char> buffer(len + 1);

	if (httpd_req_get_hdr_value_str(req_, name, buffer.data(), buffer.size()) != ESP_OK)
		return "";

	return buffer.data();
}

} // namespace scales
