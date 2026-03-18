#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "amf/interfaces.hpp"

namespace amf {

class RegistrationModule {
public:
    bool register_ue(const std::string& imsi, const std::string& tai);
    bool deregister_ue(const std::string& imsi);

    std::optional<UeContext> find_ue(const std::string& imsi) const;
    std::vector<UeContext> list_ue() const;

    bool has_ue(const std::string& imsi) const;
    std::size_t active_ue_count() const;

private:
    static std::string now_utc();

    std::map<std::string, UeContext> ue_db_;
};

}  // namespace amf
