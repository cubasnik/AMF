#include "amf/config/runtime_config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace amf {
namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> extract_json_value(const std::string& text, const std::string& key);
std::optional<std::string> extract_json_object(const std::string& text, const std::string& key);

bool is_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

bool is_valid_ipv4(const std::string& ip) {
    std::istringstream parts(ip);
    std::string part;
    int count = 0;

    while (std::getline(parts, part, '.')) {
        if (part.empty() || part.size() > 3 || !is_digits(part)) {
            return false;
        }

        const int value = std::stoi(part);
        if (value < 0 || value > 255) {
            return false;
        }

        ++count;
    }

    return count == 4;
}

bool is_valid_port(int value) {
    return value > 0 && value <= 65535;
}

bool parse_bool(const std::string& value, bool& out) {
    if (value == "true") {
        out = true;
        return true;
    }
    if (value == "false") {
        out = false;
        return true;
    }

    return false;
}

bool parse_double(const std::string& value, double& out) {
    try {
        std::size_t parsed = 0;
        out = std::stod(value, &parsed);
        return parsed == value.size();
    } catch (...) {
        return false;
    }
}

bool parse_size(const std::string& value, std::size_t& out) {
    if (!is_digits(value)) {
        return false;
    }

    try {
        out = static_cast<std::size_t>(std::stoull(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool validate_alarm_thresholds(const AlarmThresholds& thresholds, std::string& error) {
    if (thresholds.warning_error_rate_percent < 0.0 || thresholds.warning_error_rate_percent > 100.0) {
        error = "Invalid alarm_thresholds.warning_error_rate_percent in config (0..100)";
        return false;
    }
    if (thresholds.critical_error_rate_percent < 0.0 || thresholds.critical_error_rate_percent > 100.0) {
        error = "Invalid alarm_thresholds.critical_error_rate_percent in config (0..100)";
        return false;
    }
    if (thresholds.warning_error_rate_percent > thresholds.critical_error_rate_percent) {
        error = "Invalid alarm thresholds: warning_error_rate_percent must be <= critical_error_rate_percent";
        return false;
    }
    if (thresholds.critical_error_count == 0) {
        error = "Invalid alarm_thresholds.critical_error_count in config (>0)";
        return false;
    }

    return true;
}

bool is_supported_transport(const std::string& value) {
    return value == "tcp" || value == "udp";
}

bool is_supported_adapter_mode(const std::string& value) {
    return value == "mock" || value == "network";
}

bool validate_endpoint(const InterfaceEndpointConfig& endpoint, const std::string& name, std::string& error) {
    if (endpoint.address.empty()) {
        error = "Invalid network_adapters." + name + ": empty address";
        return false;
    }

    if (!is_valid_port(endpoint.port)) {
        error = "Invalid network_adapters." + name + ": invalid port";
        return false;
    }

    if (!is_supported_transport(endpoint.transport)) {
        error = "Invalid network_adapters." + name + ": transport must be tcp|udp";
        return false;
    }

    return true;
}

bool validate_network_adapters(const NetworkAdaptersConfig& adapters, std::string& error) {
    if (!is_supported_adapter_mode(adapters.mode)) {
        error = "Invalid network_adapters.mode in config: expected mock|network";
        return false;
    }

    if (adapters.sbi_resilience.timeout_ms == 0) {
        error = "Invalid network_adapters.sbi_timeout_ms in config (>0)";
        return false;
    }
    if (adapters.sbi_resilience.circuit_breaker_failure_threshold == 0) {
        error = "Invalid network_adapters.sbi_cb_failure_threshold in config (>0)";
        return false;
    }
    if (adapters.sbi_resilience.circuit_breaker_reset_seconds == 0) {
        error = "Invalid network_adapters.sbi_cb_reset_seconds in config (>0)";
        return false;
    }

    return validate_endpoint(adapters.n1, "n1", error)
        && validate_endpoint(adapters.n2, "n2", error)
        && validate_endpoint(adapters.n3, "n3", error)
        && validate_endpoint(adapters.n8, "n8", error)
        && validate_endpoint(adapters.n11, "n11", error)
        && validate_endpoint(adapters.n12, "n12", error)
        && validate_endpoint(adapters.n14, "n14", error)
        && validate_endpoint(adapters.n15, "n15", error)
        && validate_endpoint(adapters.n22, "n22", error)
        && validate_endpoint(adapters.n26, "n26", error)
        && validate_endpoint(adapters.sbi, "sbi", error);
}

bool apply_endpoint_json(const std::string& object, const std::string& prefix, InterfaceEndpointConfig& endpoint, std::string& error) {
    if (const auto value = extract_json_value(object, prefix + "_address"); value.has_value()) {
        endpoint.address = *value;
    }

    if (const auto value = extract_json_value(object, prefix + "_port"); value.has_value()) {
        if (!is_digits(*value)) {
            error = "Invalid network_adapters." + prefix + "_port in config";
            return false;
        }
        endpoint.port = static_cast<std::uint16_t>(std::stoi(*value));
    }

    if (const auto value = extract_json_value(object, prefix + "_transport"); value.has_value()) {
        endpoint.transport = *value;
    }

    return true;
}

std::optional<std::string> extract_json_value(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const auto colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) {
        return std::nullopt;
    }

    auto start = text.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (start == std::string::npos) {
        return std::nullopt;
    }

    if (text[start] == '"') {
        ++start;
        auto end = start;
        while (end < text.size()) {
            if (text[end] == '"' && text[end - 1] != '\\') {
                break;
            }
            ++end;
        }

        if (end >= text.size()) {
            return std::nullopt;
        }

        return text.substr(start, end - start);
    }

    auto end = start;
    while (end < text.size() && text[end] != ',' && text[end] != '\n' && text[end] != '\r' && text[end] != '}') {
        ++end;
    }

    return trim(text.substr(start, end - start));
}

std::optional<std::string> extract_json_object(const std::string& text, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const auto open_pos = text.find('{', key_pos + needle.size());
    if (open_pos == std::string::npos) {
        return std::nullopt;
    }

    int depth = 1;
    for (std::size_t i = open_pos + 1; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(open_pos + 1, i - open_pos - 1);
            }
        }
    }

    return std::nullopt;
}

bool apply_json_content(const std::string& text, RuntimeConfig& cfg, std::string& error) {
    if (const auto mcc = extract_json_value(text, "mcc"); mcc.has_value()) {
        cfg.cli.mcc = *mcc;
    }
    if (const auto mnc = extract_json_value(text, "mnc"); mnc.has_value()) {
        cfg.cli.mnc = *mnc;
    }

    if (const auto log_file = extract_json_value(text, "log_file"); log_file.has_value()) {
        cfg.log_file = *log_file;
    }
    if (const auto audit_file = extract_json_value(text, "audit_log_file"); audit_file.has_value()) {
        cfg.audit_log_file = *audit_file;
    }
    if (const auto policy_file = extract_json_value(text, "rbac_policy_file"); policy_file.has_value()) {
        cfg.rbac_policy_file = *policy_file;
    }

    if (const auto n2_object = extract_json_object(text, "n2"); n2_object.has_value()) {
        if (const auto n2_addr = extract_json_value(*n2_object, "local_address"); n2_addr.has_value()) {
            cfg.cli.n2.local_address = *n2_addr;
        }
        if (const auto n2_port = extract_json_value(*n2_object, "port"); n2_port.has_value()) {
            cfg.cli.n2.port = static_cast<std::uint16_t>(std::stoi(*n2_port));
        }
    }

    if (const auto sbi_object = extract_json_object(text, "sbi"); sbi_object.has_value()) {
        if (const auto sbi_addr = extract_json_value(*sbi_object, "bind_address"); sbi_addr.has_value()) {
            cfg.cli.sbi.bind_address = *sbi_addr;
        }
        if (const auto sbi_port = extract_json_value(*sbi_object, "port"); sbi_port.has_value()) {
            cfg.cli.sbi.port = static_cast<std::uint16_t>(std::stoi(*sbi_port));
        }
        if (const auto sbi_nf = extract_json_value(*sbi_object, "nf_instance"); sbi_nf.has_value()) {
            cfg.cli.sbi.nf_instance = *sbi_nf;
        }
    }

    if (const auto alarm_object = extract_json_object(text, "alarm_thresholds"); alarm_object.has_value()) {
        if (const auto warning_rate = extract_json_value(*alarm_object, "warning_error_rate_percent"); warning_rate.has_value()) {
            double parsed = 0.0;
            if (!parse_double(*warning_rate, parsed)) {
                error = "Invalid alarm_thresholds.warning_error_rate_percent in config";
                return false;
            }
            cfg.alarm_thresholds.warning_error_rate_percent = parsed;
        }
        if (const auto critical_rate = extract_json_value(*alarm_object, "critical_error_rate_percent"); critical_rate.has_value()) {
            double parsed = 0.0;
            if (!parse_double(*critical_rate, parsed)) {
                error = "Invalid alarm_thresholds.critical_error_rate_percent in config";
                return false;
            }
            cfg.alarm_thresholds.critical_error_rate_percent = parsed;
        }
        if (const auto critical_count = extract_json_value(*alarm_object, "critical_error_count"); critical_count.has_value()) {
            std::size_t parsed = 0;
            if (!parse_size(*critical_count, parsed)) {
                error = "Invalid alarm_thresholds.critical_error_count in config";
                return false;
            }
            cfg.alarm_thresholds.critical_error_count = parsed;
        }
        if (const auto admin_down_warning = extract_json_value(*alarm_object, "admin_down_warning"); admin_down_warning.has_value()) {
            bool parsed = true;
            if (!parse_bool(*admin_down_warning, parsed)) {
                error = "Invalid alarm_thresholds.admin_down_warning in config";
                return false;
            }
            cfg.alarm_thresholds.admin_down_warning = parsed;
        }
    }

    if (const auto network_object = extract_json_object(text, "network_adapters"); network_object.has_value()) {
        if (const auto mode = extract_json_value(*network_object, "mode"); mode.has_value()) {
            cfg.network_adapters.mode = *mode;
        }

        if (const auto timeout_ms = extract_json_value(*network_object, "sbi_timeout_ms"); timeout_ms.has_value()) {
            std::size_t parsed = 0;
            if (!parse_size(*timeout_ms, parsed)) {
                error = "Invalid network_adapters.sbi_timeout_ms in config";
                return false;
            }
            cfg.network_adapters.sbi_resilience.timeout_ms = parsed;
        }
        if (const auto retry_count = extract_json_value(*network_object, "sbi_retry_count"); retry_count.has_value()) {
            std::size_t parsed = 0;
            if (!parse_size(*retry_count, parsed)) {
                error = "Invalid network_adapters.sbi_retry_count in config";
                return false;
            }
            cfg.network_adapters.sbi_resilience.retry_count = parsed;
        }
        if (const auto cb_threshold = extract_json_value(*network_object, "sbi_cb_failure_threshold"); cb_threshold.has_value()) {
            std::size_t parsed = 0;
            if (!parse_size(*cb_threshold, parsed)) {
                error = "Invalid network_adapters.sbi_cb_failure_threshold in config";
                return false;
            }
            cfg.network_adapters.sbi_resilience.circuit_breaker_failure_threshold = parsed;
        }
        if (const auto cb_reset = extract_json_value(*network_object, "sbi_cb_reset_seconds"); cb_reset.has_value()) {
            std::size_t parsed = 0;
            if (!parse_size(*cb_reset, parsed)) {
                error = "Invalid network_adapters.sbi_cb_reset_seconds in config";
                return false;
            }
            cfg.network_adapters.sbi_resilience.circuit_breaker_reset_seconds = parsed;
        }

        if (!apply_endpoint_json(*network_object, "n1", cfg.network_adapters.n1, error)
            || !apply_endpoint_json(*network_object, "n2", cfg.network_adapters.n2, error)
            || !apply_endpoint_json(*network_object, "n3", cfg.network_adapters.n3, error)
            || !apply_endpoint_json(*network_object, "n8", cfg.network_adapters.n8, error)
            || !apply_endpoint_json(*network_object, "n11", cfg.network_adapters.n11, error)
            || !apply_endpoint_json(*network_object, "n12", cfg.network_adapters.n12, error)
            || !apply_endpoint_json(*network_object, "n14", cfg.network_adapters.n14, error)
            || !apply_endpoint_json(*network_object, "n15", cfg.network_adapters.n15, error)
            || !apply_endpoint_json(*network_object, "n22", cfg.network_adapters.n22, error)
            || !apply_endpoint_json(*network_object, "n26", cfg.network_adapters.n26, error)
            || !apply_endpoint_json(*network_object, "sbi", cfg.network_adapters.sbi, error)) {
            return false;
        }
    }

    if (cfg.cli.mcc.size() != 3 || !is_digits(cfg.cli.mcc)) {
        error = "Invalid mcc in config: expected 3 digits";
        return false;
    }
    if (cfg.cli.mnc.size() != 2 || !is_digits(cfg.cli.mnc)) {
        error = "Invalid mnc in config: expected 2 digits";
        return false;
    }
    if (!is_valid_ipv4(cfg.cli.n2.local_address)) {
        error = "Invalid n2.local_address in config";
        return false;
    }
    if (!is_valid_port(cfg.cli.n2.port)) {
        error = "Invalid n2.port in config";
        return false;
    }
    if (!is_valid_ipv4(cfg.cli.sbi.bind_address)) {
        error = "Invalid sbi.bind_address in config";
        return false;
    }
    if (!is_valid_port(cfg.cli.sbi.port)) {
        error = "Invalid sbi.port in config";
        return false;
    }

    if (!validate_alarm_thresholds(cfg.alarm_thresholds, error)) {
        return false;
    }

    if (!validate_network_adapters(cfg.network_adapters, error)) {
        return false;
    }

    return true;
}

bool apply_yaml_content(const std::string& text, RuntimeConfig& cfg, std::string& error) {
    enum class Section {
        Root,
        N2,
        Sbi,
        AlarmThresholds,
        NetworkAdapters,
    };

    Section section = Section::Root;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed == "n2:") {
            section = Section::N2;
            continue;
        }
        if (trimmed == "sbi:") {
            section = Section::Sbi;
            continue;
        }
        if (trimmed == "alarm_thresholds:") {
            section = Section::AlarmThresholds;
            continue;
        }
        if (trimmed == "network_adapters:") {
            section = Section::NetworkAdapters;
            continue;
        }

        const auto colon = trimmed.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const auto key = trim(trimmed.substr(0, colon));
        auto value = trim(trimmed.substr(colon + 1));
        if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() >= 2) {
            value = value.substr(1, value.size() - 2);
        }

        if (section == Section::Root) {
            if (key == "mcc") {
                cfg.cli.mcc = value;
            } else if (key == "mnc") {
                cfg.cli.mnc = value;
            } else if (key == "log_file") {
                cfg.log_file = value;
            } else if (key == "audit_log_file") {
                cfg.audit_log_file = value;
            } else if (key == "rbac_policy_file") {
                cfg.rbac_policy_file = value;
            }
        } else if (section == Section::N2) {
            if (key == "local_address") {
                cfg.cli.n2.local_address = value;
            } else if (key == "port") {
                cfg.cli.n2.port = static_cast<std::uint16_t>(std::stoi(value));
            }
        } else if (section == Section::Sbi) {
            if (key == "bind_address") {
                cfg.cli.sbi.bind_address = value;
            } else if (key == "port") {
                cfg.cli.sbi.port = static_cast<std::uint16_t>(std::stoi(value));
            } else if (key == "nf_instance") {
                cfg.cli.sbi.nf_instance = value;
            }
        } else {
            if (key == "warning_error_rate_percent") {
                double parsed = 0.0;
                if (!parse_double(value, parsed)) {
                    error = "Invalid alarm_thresholds.warning_error_rate_percent in config";
                    return false;
                }
                cfg.alarm_thresholds.warning_error_rate_percent = parsed;
            } else if (key == "critical_error_rate_percent") {
                double parsed = 0.0;
                if (!parse_double(value, parsed)) {
                    error = "Invalid alarm_thresholds.critical_error_rate_percent in config";
                    return false;
                }
                cfg.alarm_thresholds.critical_error_rate_percent = parsed;
            } else if (key == "critical_error_count") {
                std::size_t parsed = 0;
                if (!parse_size(value, parsed)) {
                    error = "Invalid alarm_thresholds.critical_error_count in config";
                    return false;
                }
                cfg.alarm_thresholds.critical_error_count = parsed;
            } else if (key == "admin_down_warning") {
                bool parsed = true;
                if (!parse_bool(value, parsed)) {
                    error = "Invalid alarm_thresholds.admin_down_warning in config";
                    return false;
                }
                cfg.alarm_thresholds.admin_down_warning = parsed;
            }
        }

        if (section == Section::NetworkAdapters) {
            if (key == "mode") {
                cfg.network_adapters.mode = value;
                continue;
            }
            if (key == "sbi_timeout_ms") {
                std::size_t parsed = 0;
                if (!parse_size(value, parsed)) {
                    error = "Invalid network_adapters.sbi_timeout_ms in config";
                    return false;
                }
                cfg.network_adapters.sbi_resilience.timeout_ms = parsed;
                continue;
            }
            if (key == "sbi_retry_count") {
                std::size_t parsed = 0;
                if (!parse_size(value, parsed)) {
                    error = "Invalid network_adapters.sbi_retry_count in config";
                    return false;
                }
                cfg.network_adapters.sbi_resilience.retry_count = parsed;
                continue;
            }
            if (key == "sbi_cb_failure_threshold") {
                std::size_t parsed = 0;
                if (!parse_size(value, parsed)) {
                    error = "Invalid network_adapters.sbi_cb_failure_threshold in config";
                    return false;
                }
                cfg.network_adapters.sbi_resilience.circuit_breaker_failure_threshold = parsed;
                continue;
            }
            if (key == "sbi_cb_reset_seconds") {
                std::size_t parsed = 0;
                if (!parse_size(value, parsed)) {
                    error = "Invalid network_adapters.sbi_cb_reset_seconds in config";
                    return false;
                }
                cfg.network_adapters.sbi_resilience.circuit_breaker_reset_seconds = parsed;
                continue;
            }

            const auto apply_endpoint_yaml = [&](const std::string& prefix, InterfaceEndpointConfig& endpoint) -> bool {
                if (key == prefix + "_address") {
                    endpoint.address = value;
                    return true;
                }
                if (key == prefix + "_port") {
                    if (!is_digits(value)) {
                        error = "Invalid network_adapters." + prefix + "_port in config";
                        return false;
                    }
                    endpoint.port = static_cast<std::uint16_t>(std::stoi(value));
                    return true;
                }
                if (key == prefix + "_transport") {
                    endpoint.transport = value;
                    return true;
                }

                return true;
            };

            if (!apply_endpoint_yaml("n1", cfg.network_adapters.n1)
                || !apply_endpoint_yaml("n2", cfg.network_adapters.n2)
                || !apply_endpoint_yaml("n3", cfg.network_adapters.n3)
                || !apply_endpoint_yaml("n8", cfg.network_adapters.n8)
                || !apply_endpoint_yaml("n11", cfg.network_adapters.n11)
                || !apply_endpoint_yaml("n12", cfg.network_adapters.n12)
                || !apply_endpoint_yaml("n14", cfg.network_adapters.n14)
                || !apply_endpoint_yaml("n15", cfg.network_adapters.n15)
                || !apply_endpoint_yaml("n22", cfg.network_adapters.n22)
                || !apply_endpoint_yaml("n26", cfg.network_adapters.n26)
                || !apply_endpoint_yaml("sbi", cfg.network_adapters.sbi)) {
                return false;
            }
        }
    }

    if (cfg.cli.mcc.size() != 3 || !is_digits(cfg.cli.mcc)) {
        error = "Invalid mcc in config: expected 3 digits";
        return false;
    }
    if (cfg.cli.mnc.size() != 2 || !is_digits(cfg.cli.mnc)) {
        error = "Invalid mnc in config: expected 2 digits";
        return false;
    }
    if (!is_valid_ipv4(cfg.cli.n2.local_address)) {
        error = "Invalid n2.local_address in config";
        return false;
    }
    if (!is_valid_port(cfg.cli.n2.port)) {
        error = "Invalid n2.port in config";
        return false;
    }
    if (!is_valid_ipv4(cfg.cli.sbi.bind_address)) {
        error = "Invalid sbi.bind_address in config";
        return false;
    }
    if (!is_valid_port(cfg.cli.sbi.port)) {
        error = "Invalid sbi.port in config";
        return false;
    }

    if (!validate_alarm_thresholds(cfg.alarm_thresholds, error)) {
        return false;
    }

    if (!validate_network_adapters(cfg.network_adapters, error)) {
        return false;
    }

    return true;
}

}  // namespace

bool load_runtime_config_file(const std::string& path, RuntimeConfig& cfg, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "Could not open config file: " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    const auto dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);

    try {
        if (ext == "json") {
            return apply_json_content(content, cfg, error);
        }

        if (ext == "yaml" || ext == "yml") {
            return apply_yaml_content(content, cfg, error);
        }
    } catch (const std::exception& ex) {
        error = std::string("Config parse exception: ") + ex.what();
        return false;
    }

    error = "Unsupported config extension. Use .json/.yaml/.yml";
    return false;
}

}  // namespace amf
