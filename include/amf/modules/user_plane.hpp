#pragma once

#include <string>

#include "amf/interfaces.hpp"

namespace amf {

class UserPlaneModule {
public:
    explicit UserPlaneModule(IN3Interface* n3);

    bool forward_n3_user_plane(bool operational, bool ue_exists, const std::string& imsi, const std::string& payload);

private:
    IN3Interface* n3_;
};

}  // namespace amf
