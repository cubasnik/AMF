#pragma once

#include <string>
#include <vector>

#include "amf/interfaces.hpp"

namespace amf {

class AmfControlService {
public:
    explicit AmfControlService(IAmfNode& node);

    bool startup();
    bool shutdown();
    bool degrade();
    bool recover();
    void heartbeat();

    bool register_ue(const std::string& imsi, const std::string& tai);
    bool deregister_ue(const std::string& imsi);

    bool send_n2(const std::string& imsi, const std::string& payload);
    bool notify_sbi(const std::string& service_name, const std::string& payload);

    bool apply_plmn(const std::string& mcc, const std::string& mnc);

    AmfStatusSnapshot status() const;
    std::vector<UeContext> ue_list() const;

private:
    static bool is_valid_imsi(const std::string& imsi);

    IAmfNode& node_;
};

}  // namespace amf
