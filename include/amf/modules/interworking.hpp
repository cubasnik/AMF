#pragma once

#include <string>

#include "amf/interfaces.hpp"

namespace amf {

class InterworkingModule {
public:
    InterworkingModule(IN14Interface* n14, IN26Interface* n26);

    bool transfer_n14_context(bool operational, bool ue_exists, const std::string& imsi, const std::string& target_amf);
    bool interwork_n26(bool operational, bool ue_exists, const std::string& imsi, const std::string& operation);

private:
    IN14Interface* n14_;
    IN26Interface* n26_;
};

}  // namespace amf
