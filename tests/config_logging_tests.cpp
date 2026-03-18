#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "amf/config/runtime_config.hpp"
#include "amf/logging/file_logger.hpp"

namespace {

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::string read_all(const std::string& path) {
    std::ifstream input(path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool test_yaml_config_load() {
    const std::string path = "test-runtime.yaml";
    {
        std::ofstream out(path);
        out << "mcc: \"250\"\n";
        out << "mnc: \"03\"\n";
        out << "log_file: \"yaml.log\"\n";
        out << "audit_log_file: \"yaml-audit.log\"\n";
        out << "rbac_policy_file: \"yaml-rbac.conf\"\n";
        out << "n2:\n";
        out << "  local_address: \"10.0.0.1\"\n";
        out << "  port: 38412\n";
        out << "sbi:\n";
        out << "  bind_address: \"10.0.0.2\"\n";
        out << "  port: 7777\n";
        out << "  nf_instance: \"amf-yaml\"\n";
        out << "alarm_thresholds:\n";
        out << "  warning_error_rate_percent: 5.0\n";
        out << "  critical_error_rate_percent: 40.0\n";
        out << "  critical_error_count: 2\n";
        out << "  admin_down_warning: false\n";
    }

    amf::RuntimeConfig cfg;
    std::string error;
    const bool loaded = amf::load_runtime_config_file(path, cfg, error);

    bool ok = true;
    ok &= check(loaded, "YAML config should load");
    ok &= check(cfg.cli.mcc == "250", "YAML mcc should match");
    ok &= check(cfg.cli.n2.local_address == "10.0.0.1", "YAML n2 address should match");
    ok &= check(cfg.cli.sbi.nf_instance == "amf-yaml", "YAML nf instance should match");
    ok &= check(cfg.alarm_thresholds.warning_error_rate_percent == 5.0, "YAML warning threshold should match");
    ok &= check(cfg.alarm_thresholds.critical_error_rate_percent == 40.0, "YAML critical threshold should match");
    ok &= check(cfg.alarm_thresholds.critical_error_count == 2, "YAML critical count should match");
    ok &= check(!cfg.alarm_thresholds.admin_down_warning, "YAML admin_down_warning should match");
    return ok;
}

bool test_json_config_load() {
    const std::string path = "test-runtime.json";
    {
        std::ofstream out(path);
        out << "{\n";
        out << "  \"mcc\": \"250\",\n";
        out << "  \"mnc\": \"03\",\n";
        out << "  \"log_file\": \"json.log\",\n";
        out << "  \"audit_log_file\": \"json-audit.log\",\n";
        out << "  \"rbac_policy_file\": \"json-rbac.conf\",\n";
        out << "  \"n2\": {\n";
        out << "    \"local_address\": \"172.16.0.1\",\n";
        out << "    \"port\": 38413\n";
        out << "  },\n";
        out << "  \"sbi\": {\n";
        out << "    \"bind_address\": \"172.16.0.2\",\n";
        out << "    \"port\": 7778,\n";
        out << "    \"nf_instance\": \"amf-json\"\n";
        out << "  },\n";
        out << "  \"alarm_thresholds\": {\n";
        out << "    \"warning_error_rate_percent\": 7.5,\n";
        out << "    \"critical_error_rate_percent\": 65.0,\n";
        out << "    \"critical_error_count\": 4,\n";
        out << "    \"admin_down_warning\": false\n";
        out << "  }\n";
        out << "}\n";
    }

    amf::RuntimeConfig cfg;
    std::string error;
    const bool loaded = amf::load_runtime_config_file(path, cfg, error);

    bool ok = true;
    ok &= check(loaded, "JSON config should load");
    ok &= check(cfg.cli.mcc == "250", "JSON mcc should match");
    ok &= check(cfg.cli.n2.port == 38413, "JSON n2 port should match");
    ok &= check(cfg.cli.sbi.port == 7778, "JSON sbi port should match");
    ok &= check(cfg.cli.sbi.nf_instance == "amf-json", "JSON nf instance should match");
    ok &= check(cfg.alarm_thresholds.warning_error_rate_percent == 7.5, "JSON warning threshold should match");
    ok &= check(cfg.alarm_thresholds.critical_error_rate_percent == 65.0, "JSON critical threshold should match");
    ok &= check(cfg.alarm_thresholds.critical_error_count == 4, "JSON critical count should match");
    ok &= check(!cfg.alarm_thresholds.admin_down_warning, "JSON admin_down_warning should match");
    return ok;
}

bool test_file_logger() {
    const std::string path = "test-amf.log";
    amf::FileLogger logger(path);

    const bool wrote = logger.log(amf::LogLevel::Info, "logger test message");
    const auto content = read_all(path);

    bool ok = true;
    ok &= check(wrote, "Logger should write message");
    ok &= check(content.find("[INFO]") != std::string::npos, "Log content should contain INFO level");
    ok &= check(content.find("logger test message") != std::string::npos, "Log content should contain message");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_yaml_config_load();
    ok &= test_json_config_load();
    ok &= test_file_logger();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] Config and logging tests passed.\n";
    return 0;
}
