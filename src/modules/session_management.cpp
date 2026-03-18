#include "amf/modules/session_management.hpp"

#include <algorithm>
#include <cctype>

namespace amf {

SessionManagementModule::SessionManagementModule(IN2Interface& n2, ISbiInterface& sbi)
    : n2_(n2), sbi_(sbi) {}

bool SessionManagementModule::send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists) {
        return false;
    }

    n2_.deliver_nas(imsi, payload);
    return true;
}

bool SessionManagementModule::notify_sbi(bool operational, const std::string& service_name, const std::string& payload) {
    if (!operational) {
        return false;
    }

    sbi_.notify_service(service_name, payload);
    return true;
}

bool SessionManagementModule::set_plmn(const std::string& mcc, const std::string& mnc) {
    if (mcc.size() != 3 || mnc.size() != 2 || !is_all_digits(mcc) || !is_all_digits(mnc)) {
        return false;
    }

    mcc_ = mcc;
    mnc_ = mnc;
    return true;
}

const std::string& SessionManagementModule::mcc() const {
    return mcc_;
}

const std::string& SessionManagementModule::mnc() const {
    return mnc_;
}

bool SessionManagementModule::is_all_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

}  // namespace amf
