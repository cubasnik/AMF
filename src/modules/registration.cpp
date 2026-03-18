#include "amf/modules/registration.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace amf {

bool RegistrationModule::register_ue(const std::string& imsi, const std::string& tai) {
    auto [it, inserted] = ue_db_.emplace(imsi, UeContext {imsi, tai, UeState::Registered, now_utc()});
    if (!inserted) {
        it->second.state = UeState::Registered;
        it->second.tai = tai;
        it->second.last_seen_utc = now_utc();
    }

    return true;
}

bool RegistrationModule::deregister_ue(const std::string& imsi) {
    auto it = ue_db_.find(imsi);
    if (it == ue_db_.end()) {
        return false;
    }

    it->second.state = UeState::Deregistered;
    it->second.last_seen_utc = now_utc();
    return true;
}

std::optional<UeContext> RegistrationModule::find_ue(const std::string& imsi) const {
    const auto it = ue_db_.find(imsi);
    if (it == ue_db_.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::vector<UeContext> RegistrationModule::list_ue() const {
    std::vector<UeContext> result;
    result.reserve(ue_db_.size());

    for (const auto& [_, ue] : ue_db_) {
        result.push_back(ue);
    }

    std::sort(result.begin(), result.end(), [](const UeContext& lhs, const UeContext& rhs) {
        return lhs.imsi < rhs.imsi;
    });

    return result;
}

bool RegistrationModule::has_ue(const std::string& imsi) const {
    return ue_db_.find(imsi) != ue_db_.end();
}

std::size_t RegistrationModule::active_ue_count() const {
    std::size_t active = 0;
    for (const auto& [_, ue] : ue_db_) {
        if (ue.state == UeState::Registered) {
            ++active;
        }
    }

    return active;
}

std::string RegistrationModule::now_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc {};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_time);
#else
    gmtime_r(&now_time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}  // namespace amf
