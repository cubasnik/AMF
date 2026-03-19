#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "amf/cli.hpp"

namespace amf {

struct InterfaceEndpointConfig {
    std::string address {"127.0.0.1"};
    std::uint16_t port {0};
    std::string transport {"tcp"};
};

struct SbiResilienceConfig {
    std::size_t timeout_ms {2000};
    std::size_t retry_count {2};
    std::size_t circuit_breaker_failure_threshold {3};
    std::size_t circuit_breaker_reset_seconds {10};
};

struct NetworkAdaptersConfig {
    std::string mode {"mock"};
    InterfaceEndpointConfig n1 {"127.0.0.1", 39001, "tcp"};
    InterfaceEndpointConfig n2 {"127.0.0.1", 38412, "udp"};
    InterfaceEndpointConfig n3 {"127.0.0.1", 2152, "udp"};
    InterfaceEndpointConfig n8 {"127.0.0.1", 39008, "tcp"};
    InterfaceEndpointConfig n11 {"127.0.0.1", 39011, "tcp"};
    InterfaceEndpointConfig n12 {"127.0.0.1", 39012, "tcp"};
    InterfaceEndpointConfig n14 {"127.0.0.1", 39014, "tcp"};
    InterfaceEndpointConfig n15 {"127.0.0.1", 39015, "tcp"};
    InterfaceEndpointConfig n22 {"127.0.0.1", 39022, "tcp"};
    InterfaceEndpointConfig n26 {"127.0.0.1", 39026, "udp"};
    InterfaceEndpointConfig sbi {"127.0.0.1", 7777, "tcp"};
    SbiResilienceConfig sbi_resilience {};
};

struct RuntimeConfig {
    AmfCliConfig cli {};
    std::string log_file {"amf.log"};
    std::string audit_log_file {"amf-audit.log"};
    std::string rbac_policy_file {"rbac-policy.conf"};
    AlarmThresholds alarm_thresholds {};
    NetworkAdaptersConfig network_adapters {};
};

bool load_runtime_config_file(const std::string& path, RuntimeConfig& cfg, std::string& error);

}  // namespace amf
