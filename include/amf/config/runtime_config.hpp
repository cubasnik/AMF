#pragma once

#include <optional>
#include <string>

#include "amf/cli.hpp"

namespace amf {

struct RuntimeConfig {
    AmfCliConfig cli {};
    std::string log_file {"amf.log"};
    std::string audit_log_file {"amf-audit.log"};
    std::string rbac_policy_file {"rbac-policy.conf"};
    AlarmThresholds alarm_thresholds {};
};

bool load_runtime_config_file(const std::string& path, RuntimeConfig& cfg, std::string& error);

}  // namespace amf
