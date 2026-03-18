#include "amf/modules/user_plane.hpp"

namespace amf {

UserPlaneModule::UserPlaneModule(IN3Interface* n3)
    : n3_(n3) {}

bool UserPlaneModule::forward_n3_user_plane(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload) {
    if (!operational || !ue_exists || n3_ == nullptr) {
        return false;
    }

    n3_->forward_user_plane(imsi, payload);
    return true;
}

}  // namespace amf
