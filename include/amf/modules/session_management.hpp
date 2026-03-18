#pragma once

#include <string>

#include "amf/interfaces.hpp"

namespace amf {

class SessionManagementModule {
public:
    SessionManagementModule(IN2Interface& n2, ISbiInterface& sbi);

    bool send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);
    bool notify_sbi(bool operational, const std::string& service_name, const std::string& payload);

    bool set_plmn(const std::string& mcc, const std::string& mnc);
    const std::string& mcc() const;
    const std::string& mnc() const;

private:
    static bool is_all_digits(const std::string& value);

    IN2Interface& n2_;
    ISbiInterface& sbi_;
    std::string mcc_ {"250"};
    std::string mnc_ {"03"};
};

}  // namespace amf
