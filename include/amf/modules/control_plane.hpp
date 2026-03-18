#pragma once

#include <string>

#include "amf/interfaces.hpp"

namespace amf {

class ControlPlaneModule {
public:
    ControlPlaneModule(
        IN1Interface* n1,
        IN2Interface* n2,
        ISbiInterface* sbi,
        IN8Interface* n8,
        IN11Interface* n11,
        IN12Interface* n12,
        IN15Interface* n15,
        IN22Interface* n22);

    bool send_n1_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);
    bool send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);
    bool notify_sbi(bool operational, const std::string& service_name, const std::string& payload);

    bool query_n8_subscription(bool operational, bool ue_exists, const std::string& imsi);
    bool manage_n11_pdu_session(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation);
    bool authenticate_n12(bool operational, bool ue_exists, const std::string& imsi);
    bool query_n15_policy(bool operational, bool ue_exists, const std::string& imsi);
    bool select_n22_slice(bool operational, bool ue_exists, const std::string& imsi, const std::string& snssai);

    bool set_plmn(const std::string& mcc, const std::string& mnc);
    const std::string& mcc() const;
    const std::string& mnc() const;

private:
    static bool is_all_digits(const std::string& value);

    IN1Interface* n1_;
    IN2Interface* n2_;
    ISbiInterface* sbi_;
    IN8Interface* n8_;
    IN11Interface* n11_;
    IN12Interface* n12_;
    IN15Interface* n15_;
    IN22Interface* n22_;
    std::string mcc_ {"250"};
    std::string mnc_ {"03"};
};

}  // namespace amf
