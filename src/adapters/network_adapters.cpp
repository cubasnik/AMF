#include "amf/adapters/network_adapters.hpp"

#include <cstring>
#include <sstream>
#include <string>
#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <iomanip>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace amf {
namespace {

bool is_timeout_error_non_windows() {
#if defined(_WIN32)
    return false;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT;
#endif
}

enum class SbiSendFailureKind {
    None,
    Timeout,
    ConnectFail,
    Non2xx,
    Protocol,
    CircuitOpen,
    Transport,
};

struct ParsedHttpResponse {
    int status_code {0};
    std::string status_text;
};

class SocketRuntime {
public:
    SocketRuntime() {
#if defined(_WIN32)
        WSADATA data {};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        initialized_ = true;
#endif
    }

    ~SocketRuntime() {
#if defined(_WIN32)
        if (initialized_) {
            WSACleanup();
        }
#endif
    }

    bool ok() const {
        return initialized_;
    }

private:
    bool initialized_ {false};
};

SocketRuntime& runtime() {
    static SocketRuntime r;
    return r;
}

void close_socket(int fd) {
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(fd));
#else
    close(fd);
#endif
}

bool send_udp(const InterfaceEndpointConfig& endpoint, const std::string& payload, std::string& error) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    const auto port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.address.c_str(), port.c_str(), &hints, &result) != 0) {
        error = "getaddrinfo failed";
        return false;
    }

    bool sent = false;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        const int sock = static_cast<int>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
#if defined(_WIN32)
        if (sock == static_cast<int>(INVALID_SOCKET)) {
            continue;
        }
#else
        if (sock < 0) {
            continue;
        }
#endif

        const int rc = static_cast<int>(sendto(sock, payload.c_str(), static_cast<int>(payload.size()), 0, it->ai_addr, static_cast<int>(it->ai_addrlen)));
        close_socket(sock);
        if (rc >= 0) {
            sent = true;
            break;
        }
    }

    freeaddrinfo(result);

    if (!sent) {
        error = "udp send failed";
    }
    return sent;
}

bool send_tcp(const InterfaceEndpointConfig& endpoint, const std::string& payload, std::string& error) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const auto port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.address.c_str(), port.c_str(), &hints, &result) != 0) {
        error = "getaddrinfo failed";
        return false;
    }

    bool sent = false;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        const int sock = static_cast<int>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
#if defined(_WIN32)
        if (sock == static_cast<int>(INVALID_SOCKET)) {
            continue;
        }
#else
        if (sock < 0) {
            continue;
        }
#endif

        if (connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            const int rc = static_cast<int>(send(sock, payload.c_str(), static_cast<int>(payload.size()), 0));
            if (rc >= 0) {
                sent = true;
            }
        }

        close_socket(sock);
        if (sent) {
            break;
        }
    }

    freeaddrinfo(result);

    if (!sent) {
        error = "tcp send failed";
    }
    return sent;
}

bool send_tcp_request_and_read_response(
    const InterfaceEndpointConfig& endpoint,
    std::size_t timeout_ms,
    const std::string& request,
    std::string& response,
    SbiSendFailureKind& failure_kind,
    std::string& error) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const auto port = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.address.c_str(), port.c_str(), &hints, &result) != 0) {
        error = "getaddrinfo failed";
        failure_kind = SbiSendFailureKind::ConnectFail;
        return false;
    }

    bool ok = false;
    bool saw_timeout = false;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        const int sock = static_cast<int>(socket(it->ai_family, it->ai_socktype, it->ai_protocol));
#if defined(_WIN32)
        if (sock == static_cast<int>(INVALID_SOCKET)) {
            continue;
        }
        const int timeout_ms_int = static_cast<int>(timeout_ms);
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms_int), sizeof(timeout_ms_int));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms_int), sizeof(timeout_ms_int));
#else
        if (sock < 0) {
            continue;
        }
        const timeval timeout {
            static_cast<long>(timeout_ms / 1000),
            static_cast<long>((timeout_ms % 1000) * 1000)
        };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif

        if (connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) != 0) {
#if defined(_WIN32)
            saw_timeout = saw_timeout || (WSAGetLastError() == WSAETIMEDOUT);
#else
            saw_timeout = saw_timeout || is_timeout_error_non_windows();
#endif
            close_socket(sock);
            continue;
        }

        const int sent = static_cast<int>(send(sock, request.c_str(), static_cast<int>(request.size()), 0));
        if (sent < 0) {
#if defined(_WIN32)
            saw_timeout = saw_timeout || (WSAGetLastError() == WSAETIMEDOUT);
#else
            saw_timeout = saw_timeout || is_timeout_error_non_windows();
#endif
            close_socket(sock);
            continue;
        }

        response.clear();
        std::array<char, 2048> buffer {};
        while (true) {
            const int rc = static_cast<int>(recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0));
            if (rc <= 0) {
#if defined(_WIN32)
                saw_timeout = saw_timeout || (WSAGetLastError() == WSAETIMEDOUT);
#else
                saw_timeout = saw_timeout || is_timeout_error_non_windows();
#endif
                break;
            }

            response.append(buffer.data(), static_cast<std::size_t>(rc));
            if (response.size() >= 512U * 1024U) {
                break;
            }
        }

        close_socket(sock);
        if (response.empty()) {
            continue;
        }

        ok = true;
        break;
    }

    freeaddrinfo(result);
    if (!ok) {
        if (saw_timeout) {
            error = "timeout";
            failure_kind = SbiSendFailureKind::Timeout;
        } else {
            error = "connect/send failed";
            failure_kind = SbiSendFailureKind::ConnectFail;
        }
    }
    return ok;
}

bool send_payload(const InterfaceEndpointConfig& endpoint, const std::string& payload, std::string& error) {
    if (!runtime().ok()) {
        error = "socket runtime init failed";
        return false;
    }

    if (endpoint.address.empty() || endpoint.port == 0) {
        error = "invalid endpoint config";
        return false;
    }

    if (endpoint.transport == "udp") {
        return send_udp(endpoint, payload, error);
    }

    if (endpoint.transport == "tcp") {
        return send_tcp(endpoint, payload, error);
    }

    error = "unsupported transport: " + endpoint.transport;
    return false;
}

std::string now_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc {};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_time);
#else
    gmtime_r(&now_time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }

    return out;
}

std::string sanitize_http_path_segment(const std::string& value) {
    if (value.empty()) {
        return "sbi";
    }

    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.') {
            out.push_back(ch);
        } else {
            out.push_back('-');
        }
    }

    return out;
}

std::string build_ngap_initial_ue_message(const std::string& imsi, const std::string& nas_payload) {
    std::ostringstream pdu;
    pdu << "NGAP/1\n";
    pdu << "procedure=InitialUEMessage\n";
    pdu << "ran-ue-ngap-id=" << imsi << "\n";
    pdu << "nas-pdu=" << nas_payload << "\n";
    pdu << "timestamp=" << now_utc() << "\n";
    return pdu.str();
}

std::string build_sbi_http_request(const InterfaceEndpointConfig& endpoint, const std::string& service_name, const std::string& payload) {
    const std::string escaped_service = json_escape(service_name);
    const std::string escaped_payload = json_escape(payload);

    std::ostringstream body;
    body << "{"
         << "\"service\":\"" << escaped_service << "\"," 
         << "\"payload\":\"" << escaped_payload << "\"," 
         << "\"timestamp\":\"" << now_utc() << "\""
         << "}";

    const std::string body_text = body.str();
    const std::string path = "/namf/" + sanitize_http_path_segment(service_name);

    std::ostringstream request;
    request << "POST " << path << " HTTP/1.1\r\n";
    request << "Host: " << endpoint.address << ':' << endpoint.port << "\r\n";
    request << "User-Agent: vAMF/1.0\r\n";
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body_text.size() << "\r\n";
    request << "Connection: close\r\n\r\n";
    request << body_text;
    return request.str();
}

int parse_http_status_code(const std::string& response) {
    const std::size_t first_sp = response.find(' ');
    if (first_sp == std::string::npos || first_sp < 8) {
        return -1;
    }

    const std::string version = response.substr(0, first_sp);
    if (version.rfind("HTTP/", 0) != 0) {
        return -1;
    }
    if (version.size() != 8 || version[5] < '0' || version[5] > '9' || version[6] != '.' || version[7] < '0' || version[7] > '9') {
        return -1;
    }

    if (first_sp + 4 > response.size()) {
        return -1;
    }
    const std::string code_text = response.substr(first_sp + 1, 3);
    if (code_text[0] < '0' || code_text[0] > '9' || code_text[1] < '0' || code_text[1] > '9' || code_text[2] < '0' || code_text[2] > '9') {
        return -1;
    }

    if (first_sp + 4 < response.size() && response[first_sp + 4] != ' ') {
        return -1;
    }

    return std::stoi(code_text);
}

bool header_has_chunked_encoding(const std::string& header_value) {
    std::string value = header_value;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value.find("chunked") != std::string::npos;
}

bool try_parse_content_length(const std::string& headers, std::size_t& out_len) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    const std::string key = "\r\ncontent-length:";
    const std::size_t pos = lower.find(key);
    if (pos == std::string::npos) {
        return false;
    }

    std::size_t start = pos + key.size();
    while (start < lower.size() && (lower[start] == ' ' || lower[start] == '\t')) {
        ++start;
    }
    std::size_t end = start;
    while (end < lower.size() && lower[end] >= '0' && lower[end] <= '9') {
        ++end;
    }
    if (end == start) {
        return false;
    }

    try {
        out_len = static_cast<std::size_t>(std::stoull(lower.substr(start, end - start)));
    } catch (...) {
        return false;
    }
    return true;
}

bool try_decode_chunked_body(const std::string& encoded, std::string& decoded) {
    decoded.clear();
    std::size_t pos = 0;
    while (pos < encoded.size()) {
        const std::size_t line_end = encoded.find("\r\n", pos);
        if (line_end == std::string::npos) {
            return false;
        }

        std::string size_text = encoded.substr(pos, line_end - pos);
        const std::size_t semi = size_text.find(';');
        if (semi != std::string::npos) {
            size_text = size_text.substr(0, semi);
        }
        if (size_text.empty()) {
            return false;
        }

        std::size_t chunk_size = 0;
        try {
            chunk_size = static_cast<std::size_t>(std::stoull(size_text, nullptr, 16));
        } catch (...) {
            return false;
        }

        pos = line_end + 2;
        if (chunk_size == 0) {
            const std::size_t trailer_end = encoded.find("\r\n\r\n", pos);
            if (trailer_end != std::string::npos) {
                return true;
            }
            if (encoded.size() >= pos + 2 && encoded.substr(pos, 2) == "\r\n") {
                return true;
            }
            return false;
        }

        if (pos + chunk_size + 2 > encoded.size()) {
            return false;
        }

        decoded.append(encoded, pos, chunk_size);
        pos += chunk_size;
        if (encoded[pos] != '\r' || encoded[pos + 1] != '\n') {
            return false;
        }
        pos += 2;
    }

    return false;
}

bool parse_http_response(const std::string& raw_response, ParsedHttpResponse& parsed, std::string& error) {
    const std::size_t header_end = raw_response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        error = "incomplete headers";
        return false;
    }

    const std::size_t status_end = raw_response.find("\r\n");
    if (status_end == std::string::npos || status_end > header_end) {
        error = "missing status-line";
        return false;
    }

    const std::string status_line = raw_response.substr(0, status_end);
    const int status_code = parse_http_status_code(status_line);
    if (status_code < 100 || status_code > 599) {
        error = "invalid status-line";
        return false;
    }

    const std::string headers = raw_response.substr(status_end, header_end - status_end + 2);
    const std::string body = raw_response.substr(header_end + 4);

    std::string headers_lower = headers;
    std::transform(headers_lower.begin(), headers_lower.end(), headers_lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (header_has_chunked_encoding(headers_lower)) {
        std::string decoded;
        if (!try_decode_chunked_body(body, decoded)) {
            error = "invalid chunked body";
            return false;
        }
    } else {
        std::size_t content_length = 0;
        if (try_parse_content_length(headers, content_length) && body.size() < content_length) {
            error = "incomplete body";
            return false;
        }
    }

    parsed.status_code = status_code;
    parsed.status_text = status_line;
    return true;
}

void report_send(
    const char* iface,
    const InterfaceEndpointConfig& endpoint,
    const std::string& payload,
    std::ostream& out,
    FileLogger* logger) {
    std::string error;
    const bool ok = send_payload(endpoint, payload, error);

    if (ok) {
        out << "[" << iface << "] sent to " << endpoint.address << ':' << endpoint.port << " via " << endpoint.transport << "\n";
        if (logger != nullptr) {
            logger->log(LogLevel::Info, std::string(iface) + " network send ok endpoint=" + endpoint.address + ":" + std::to_string(endpoint.port));
        }
        return;
    }

    out << "[" << iface << "] send failed: " << error << "\n";
    if (logger != nullptr) {
        logger->log(LogLevel::Error, std::string(iface) + " network send failed error=" + error);
    }
}

}  // namespace

NetworkN2Adapter::NetworkN2Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN2Adapter::deliver_nas(const std::string& imsi, const std::string& payload) {
    const std::string ngap_pdu = payload.rfind("NGAP/1\n", 0) == 0 ? payload : build_ngap_initial_ue_message(imsi, payload);
    report_send("N2", endpoint_, ngap_pdu, out_, logger_);
}

NetworkSbiAdapter::NetworkSbiAdapter(
    const InterfaceEndpointConfig& endpoint,
    const SbiResilienceConfig& resilience,
    std::ostream& out,
    FileLogger* logger)
    : endpoint_(endpoint), resilience_(resilience), out_(out), logger_(logger) {}

bool NetworkSbiAdapter::notify_service(const std::string& service_name, const std::string& payload) {
    const auto now = std::chrono::system_clock::now();
    if (circuit_open_until_ > now) {
        ++counters_.circuit_open_rejections;
        counters_.circuit_open = true;
        last_failure_reason_ = "circuit-open";
        out_ << "[SBI] send rejected: circuit-breaker open\n";
        if (logger_ != nullptr) {
            logger_->log(LogLevel::Warning, "SBI notify rejected by circuit-breaker");
        }
        return false;
    }
    counters_.circuit_open = false;

    if (endpoint_.transport != "tcp") {
        ++consecutive_failures_;
        out_ << "[SBI] send failed: unsupported transport for HTTP " << endpoint_.transport << "\n";
        if (logger_ != nullptr) {
            logger_->log(LogLevel::Error, "SBI notify failed unsupported transport=" + endpoint_.transport);
        }
        last_failure_reason_ = "connect-fail";
        ++counters_.connect_failures;
        if (consecutive_failures_ >= resilience_.circuit_breaker_failure_threshold) {
            circuit_open_until_ = std::chrono::system_clock::now() + std::chrono::seconds(resilience_.circuit_breaker_reset_seconds);
            counters_.circuit_open = true;
        }
        return false;
    }

    if (!runtime().ok()) {
        ++consecutive_failures_;
        out_ << "[SBI] send failed: socket runtime init failed\n";
        if (logger_ != nullptr) {
            logger_->log(LogLevel::Error, "SBI notify failed socket runtime init failed");
        }
        last_failure_reason_ = "connect-fail";
        ++counters_.connect_failures;
        if (consecutive_failures_ >= resilience_.circuit_breaker_failure_threshold) {
            circuit_open_until_ = std::chrono::system_clock::now() + std::chrono::seconds(resilience_.circuit_breaker_reset_seconds);
            counters_.circuit_open = true;
        }
        return false;
    }

    const std::string http_request = build_sbi_http_request(endpoint_, service_name, payload);
    const std::size_t attempts_total = resilience_.retry_count + 1;
    SbiSendFailureKind failure_kind = SbiSendFailureKind::None;
    std::string error;

    for (std::size_t attempt = 0; attempt < attempts_total; ++attempt) {
        std::string response;
        std::string attempt_error;
        SbiSendFailureKind attempt_kind = SbiSendFailureKind::None;

        if (!send_tcp_request_and_read_response(endpoint_, resilience_.timeout_ms, http_request, response, attempt_kind, attempt_error)) {
            failure_kind = attempt_kind;
            error = attempt_error;
            continue;
        }

        ParsedHttpResponse parsed_response {};
        std::string parse_error;
        if (!parse_http_response(response, parsed_response, parse_error)) {
            failure_kind = SbiSendFailureKind::Protocol;
            error = "http parse failed: " + parse_error;
            break;
        }

        const int status_code = parsed_response.status_code;
        const bool ok = status_code >= 200 && status_code < 300;
        out_ << "[SBI] response status=" << status_code << " endpoint=" << endpoint_.address << ':' << endpoint_.port << "\n";
        if (logger_ != nullptr) {
            logger_->log(ok ? LogLevel::Info : LogLevel::Error,
                "SBI notify response status=" + std::to_string(status_code) + " endpoint=" + endpoint_.address + ":" + std::to_string(endpoint_.port));
        }

        if (ok) {
            consecutive_failures_ = 0;
            counters_.circuit_open = false;
            last_failure_reason_ = "ok";
            return true;
        }

        failure_kind = SbiSendFailureKind::Non2xx;
        error = "http status " + std::to_string(status_code);
        break;
    }

    ++consecutive_failures_;
    switch (failure_kind) {
        case SbiSendFailureKind::Timeout:
            ++counters_.timeout_failures;
            last_failure_reason_ = "timeout";
            break;
        case SbiSendFailureKind::ConnectFail:
            ++counters_.connect_failures;
            last_failure_reason_ = "connect-fail";
            break;
        case SbiSendFailureKind::Non2xx:
            ++counters_.non_2xx_failures;
            last_failure_reason_ = "non-2xx";
            break;
        case SbiSendFailureKind::Protocol:
            ++counters_.non_2xx_failures;
            last_failure_reason_ = "protocol-error";
            break;
        default:
            ++counters_.connect_failures;
            last_failure_reason_ = "connect-fail";
            break;
    }

    if (consecutive_failures_ >= resilience_.circuit_breaker_failure_threshold) {
        circuit_open_until_ = std::chrono::system_clock::now() + std::chrono::seconds(resilience_.circuit_breaker_reset_seconds);
        counters_.circuit_open = true;
    }

    out_ << "[SBI] send failed: " << error << "\n";
    if (logger_ != nullptr) {
        logger_->log(LogLevel::Error, "SBI notify failed error=" + error + " reason=" + last_failure_reason_);
    }

    return false;
}

std::string NetworkSbiAdapter::last_failure_reason() const {
    return last_failure_reason_;
}

std::optional<SbiFailureCounters> NetworkSbiAdapter::failure_counters() const {
    return counters_;
}

NetworkN1Adapter::NetworkN1Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN1Adapter::send_nas_to_ue(const std::string& imsi, const std::string& payload) {
    std::ostringstream msg;
    msg << "N1NAS/1\n";
    msg << "imsi=" << imsi << "\n";
    msg << "nas-pdu=" << payload << "\n";
    msg << "timestamp=" << now_utc() << "\n";
    report_send("N1", endpoint_, msg.str(), out_, logger_);
}

NetworkN3Adapter::NetworkN3Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN3Adapter::forward_user_plane(const std::string& imsi, const std::string& payload) {
    if (payload.rfind("GTPU/1\n", 0) == 0) {
        report_send("N3", endpoint_, payload, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N3|imsi=" << imsi << "|payload=" << payload;
    report_send("N3", endpoint_, msg.str(), out_, logger_);
}

NetworkN8Adapter::NetworkN8Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN8Adapter::query_subscription(const std::string& imsi, const std::string& request) {
    if (request.rfind("N8SBI/1\n", 0) == 0) {
        report_send("N8", endpoint_, request, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N8|imsi=" << imsi << "|request=" << request;
    report_send("N8", endpoint_, msg.str(), out_, logger_);
}

NetworkN11Adapter::NetworkN11Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN11Adapter::manage_pdu_session(const std::string& imsi, const std::string& operation) {
    if (operation.rfind("N11SBI/1\n", 0) == 0) {
        report_send("N11", endpoint_, operation, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N11|imsi=" << imsi << "|operation=" << operation;
    report_send("N11", endpoint_, msg.str(), out_, logger_);
}

NetworkN12Adapter::NetworkN12Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN12Adapter::authenticate_ue(const std::string& imsi, const std::string& request) {
    if (request.rfind("N12SBI/1\n", 0) == 0) {
        report_send("N12", endpoint_, request, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N12|imsi=" << imsi << "|request=" << request;
    report_send("N12", endpoint_, msg.str(), out_, logger_);
}

NetworkN14Adapter::NetworkN14Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN14Adapter::transfer_amf_context(const std::string& imsi, const std::string& request) {
    if (request.rfind("N14SBI/1\n", 0) == 0) {
        report_send("N14", endpoint_, request, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N14|imsi=" << imsi << "|request=" << request;
    report_send("N14", endpoint_, msg.str(), out_, logger_);
}

NetworkN15Adapter::NetworkN15Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN15Adapter::query_policy(const std::string& imsi, const std::string& request) {
    if (request.rfind("N15SBI/1\n", 0) == 0) {
        report_send("N15", endpoint_, request, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N15|imsi=" << imsi << "|request=" << request;
    report_send("N15", endpoint_, msg.str(), out_, logger_);
}

NetworkN22Adapter::NetworkN22Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN22Adapter::select_network_slice(const std::string& imsi, const std::string& request) {
    if (request.rfind("N22SBI/1\n", 0) == 0) {
        report_send("N22", endpoint_, request, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N22|imsi=" << imsi << "|request=" << request;
    report_send("N22", endpoint_, msg.str(), out_, logger_);
}

NetworkN26Adapter::NetworkN26Adapter(const InterfaceEndpointConfig& endpoint, std::ostream& out, FileLogger* logger)
    : endpoint_(endpoint), out_(out), logger_(logger) {}

void NetworkN26Adapter::interwork_with_mme(const std::string& imsi, const std::string& operation) {
    if (operation.rfind("GTPV2C/1\n", 0) == 0) {
        report_send("N26", endpoint_, operation, out_, logger_);
        return;
    }

    std::ostringstream msg;
    msg << "N26|imsi=" << imsi << "|operation=" << operation;
    report_send("N26", endpoint_, msg.str(), out_, logger_);
}

}  // namespace amf
