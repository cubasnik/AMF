#include "amf/modules/control_plane.hpp"

#include <algorithm>
#include <cctype>

namespace amf {

ControlPlaneModule::ControlPlaneModule(
    IN1Interface* n1,
    IN2Interface* n2,
    ISbiInterface* sbi,
    IN8Interface* n8,
    IN11Interface* n11,
    IN12Interface* n12,
    IN15Interface* n15,
    IN22Interface* n22)
    : n1_(n1), n2_(n2), sbi_(sbi), n8_(n8), n11_(n11), n12_(n12), n15_(n15), n22_(n22) {}

bool ControlPlaneModule::send_n1_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n1_ == nullptr) {
        return false;
    }

    n1_->send_nas_to_ue(imsi, payload);
    return true;
}

bool ControlPlaneModule::send_n2_nas(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n2_ == nullptr) {
        return false;
    }

    n2_->deliver_nas(imsi, payload);
    return true;
}

bool ControlPlaneModule::notify_sbi(bool operational, const std::string& service_name, const std::string& payload) {
    if (!operational || sbi_ == nullptr) {
        return false;
    }

    sbi_->notify_service(service_name, payload);
    return true;
}

bool ControlPlaneModule::query_n8_subscription(bool operational, bool ue_exists, const std::string& imsi) {
    if (!operational || !ue_exists || n8_ == nullptr) {
        return false;
    }

    n8_->query_subscription(imsi);
    return true;
}

bool ControlPlaneModule::manage_n11_pdu_session(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation) {
    if (!operational || !ue_exists || n11_ == nullptr || operation.empty()) {
        return false;
    }

    n11_->manage_pdu_session(imsi, operation);
    return true;
}

bool ControlPlaneModule::authenticate_n12(bool operational, bool ue_exists, const std::string& imsi) {
    if (!operational || !ue_exists || n12_ == nullptr) {
        return false;
    }

    n12_->authenticate_ue(imsi);
    return true;
}

bool ControlPlaneModule::query_n15_policy(bool operational, bool ue_exists, const std::string& imsi) {
    if (!operational || !ue_exists || n15_ == nullptr) {
        return false;
    }

    n15_->query_policy(imsi);
    return true;
}

bool ControlPlaneModule::select_n22_slice(bool operational, bool ue_exists, const std::string& imsi, const std::string& snssai) {
    if (!operational || !ue_exists || n22_ == nullptr || snssai.empty()) {
        return false;
    }

    n22_->select_network_slice(imsi, snssai);
    return true;
}

bool ControlPlaneModule::set_plmn(const std::string& mcc, const std::string& mnc) {
    if (mcc.size() != 3 || mnc.size() != 2 || !is_all_digits(mcc) || !is_all_digits(mnc)) {
        return false;
    }

    mcc_ = mcc;
    mnc_ = mnc;
    return true;
}

const std::string& ControlPlaneModule::mcc() const {
    return mcc_;
}

const std::string& ControlPlaneModule::mnc() const {
    return mnc_;
}

bool ControlPlaneModule::is_all_digits(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

}  // namespace amf
