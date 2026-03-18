#include "amf/app/amf_services.hpp"

#include <algorithm>
#include <cctype>

namespace amf {

AmfControlService::AmfControlService(IAmfNode& node)
    : node_(node) {}

bool AmfControlService::startup() {
    return node_.start();
}

bool AmfControlService::shutdown() {
    return node_.stop();
}

bool AmfControlService::degrade() {
    return node_.set_degraded();
}

bool AmfControlService::recover() {
    return node_.recover();
}

void AmfControlService::heartbeat() {
    node_.tick();
}

bool AmfControlService::register_ue(const std::string& imsi, const std::string& tai) {
    if (!is_valid_imsi(imsi) || tai.empty()) {
        return false;
    }

    return node_.register_ue(imsi, tai);
}

bool AmfControlService::deregister_ue(const std::string& imsi) {
    if (!is_valid_imsi(imsi)) {
        return false;
    }

    return node_.deregister_ue(imsi);
}

bool AmfControlService::send_n2(const std::string& imsi, const std::string& payload) {
    if (!is_valid_imsi(imsi) || payload.empty()) {
        return false;
    }

    return node_.send_n2_nas(imsi, payload);
}

bool AmfControlService::notify_sbi(const std::string& service_name, const std::string& payload) {
    if (service_name.empty() || payload.empty()) {
        return false;
    }

    return node_.notify_sbi(service_name, payload);
}

bool AmfControlService::apply_plmn(const std::string& mcc, const std::string& mnc) {
    return node_.set_plmn(mcc, mnc);
}

AmfStatusSnapshot AmfControlService::status() const {
    return node_.status();
}

std::vector<UeContext> AmfControlService::ue_list() const {
    return node_.list_ue();
}

bool AmfControlService::is_valid_imsi(const std::string& imsi) {
    if (imsi.size() < 8 || imsi.size() > 16) {
        return false;
    }

    return std::all_of(imsi.begin(), imsi.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

}  // namespace amf
