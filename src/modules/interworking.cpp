#include "amf/modules/interworking.hpp"

namespace amf {

InterworkingModule::InterworkingModule(IN14Interface* n14, IN26Interface* n26)
    : n14_(n14), n26_(n26) {}

bool InterworkingModule::transfer_n14_context(bool operational, bool ue_exists, const std::string& imsi, const std::string& target_amf) {
    if (!operational || !ue_exists || n14_ == nullptr || target_amf.empty()) {
        return false;
    }

    n14_->transfer_amf_context(imsi, target_amf);
    return true;
}

bool InterworkingModule::interwork_n26(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation) {
    if (!operational || !ue_exists || n26_ == nullptr || operation.empty()) {
        return false;
    }

    n26_->interwork_with_mme(imsi, operation);
    return true;
}

}  // namespace amf
