#include "amf/amf.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace amf {

namespace {

std::string now_utc() {
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

}  // namespace

const char* to_string(AmfState state) {
    switch (state) {
        case AmfState::Idle:
            return "IDLE";
        case AmfState::Initializing:
            return "INITIALIZING";
        case AmfState::Running:
            return "RUNNING";
        case AmfState::Degraded:
            return "DEGRADED";
        case AmfState::Stopped:
            return "STOPPED";
    }

    return "UNKNOWN";
}

const char* to_string(UeState state) {
    switch (state) {
        case UeState::Registered:
            return "REGISTERED";
        case UeState::Deregistered:
            return "DEREGISTERED";
    }

    return "UNKNOWN";
}

AmfNode::AmfNode(IN2Interface& n2, ISbiInterface& sbi, AmfPeerInterfaces peers)
        : n2_(n2), sbi_(sbi), peers_(peers), control_plane_(peers_.n1, &n2_, &sbi_, peers_.n8, peers_.n11, peers_.n12, peers_.n15, peers_.n22),
            user_plane_(peers_.n3), interworking_(peers_.n14, peers_.n26) {
    init_interface_diagnostics();
}

bool AmfNode::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mobility_.start()) {
        return false;
    }

    ++stats_.starts;
    return true;
}

bool AmfNode::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!mobility_.stop()) {
        return false;
    }

    ++stats_.stops;
    return true;
}

bool AmfNode::set_degraded() {
    std::lock_guard<std::mutex> lock(mutex_);
    return mobility_.set_degraded();
}

bool AmfNode::recover() {
    std::lock_guard<std::mutex> lock(mutex_);
    return mobility_.recover();
}

void AmfNode::tick() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (mobility_.tick()) {
        ++stats_.heartbeat_ticks;
    }
}

bool AmfNode::register_ue(const std::string& imsi, const std::string& tai) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_operational()) {
        return false;
    }

    registration_.register_ue(imsi, tai);

    ++stats_.ue_registrations;
    return true;
}

bool AmfNode::deregister_ue(const std::string& imsi) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_operational()) {
        return false;
    }

    if (!registration_.deregister_ue(imsi)) {
        return false;
    }

    ++stats_.ue_deregistrations;

    return true;
}

std::optional<UeContext> AmfNode::find_ue(const std::string& imsi) const {
    std::lock_guard<std::mutex> lock(mutex_);

    return registration_.find_ue(imsi);
}

std::vector<UeContext> AmfNode::list_ue() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return registration_.list_ue();
}

bool AmfNode::send_n2_nas(const std::string& imsi, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool sent = control_plane_.send_n2_nas(operational, registration_.has_ue(imsi), imsi, payload);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N2", sent, operational ? "service-reject" : "admin-down", latency_ms);
    if (!sent) {
        return false;
    }

    ++stats_.n2_signaling_messages;

    return true;
}

bool AmfNode::notify_sbi(const std::string& service_name, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool notified = control_plane_.notify_sbi(operational, service_name, payload);
    const std::string reason = operational ? sbi_.last_failure_reason() : "admin-down";
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("SBI", notified, reason.empty() ? "service-reject" : reason, latency_ms);

    if (const auto counters = sbi_.failure_counters(); counters.has_value()) {
        auto it = interface_diagnostics_.find("SBI");
        if (it != interface_diagnostics_.end()) {
            it->second.sbi_timeout_failures = counters->timeout_failures;
            it->second.sbi_connect_failures = counters->connect_failures;
            it->second.sbi_non_2xx_failures = counters->non_2xx_failures;
            it->second.sbi_circuit_open_rejections = counters->circuit_open_rejections;
            it->second.sbi_circuit_open = counters->circuit_open;
        }
    }

    if (!notified) {
        return false;
    }

    ++stats_.sbi_notifications;

    return true;
}

bool AmfNode::send_n1_nas(const std::string& imsi, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool sent = control_plane_.send_n1_nas(operational, registration_.has_ue(imsi), imsi, payload);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N1", sent, peers_.n1 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!sent) {
        return false;
    }
    ++stats_.n1_messages;
    return true;
}

bool AmfNode::forward_n3_user_plane(const std::string& imsi, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool forwarded = user_plane_.forward_n3_user_plane(operational, registration_.has_ue(imsi), imsi, payload);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N3", forwarded, peers_.n3 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!forwarded) {
        return false;
    }
    ++stats_.n3_user_plane_packets;
    return true;
}

bool AmfNode::query_n8_subscription(const std::string& imsi, const std::string& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool queried = control_plane_.query_n8_subscription(operational, registration_.has_ue(imsi), imsi, request);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N8", queried, peers_.n8 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!queried) {
        return false;
    }
    ++stats_.n8_subscription_queries;
    return true;
}

bool AmfNode::manage_n11_pdu_session(const std::string& imsi, const std::string& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool managed = control_plane_.manage_n11_pdu_session(operational, registration_.has_ue(imsi), imsi, operation);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N11", managed, peers_.n11 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!managed) {
        return false;
    }
    ++stats_.n11_pdu_session_ops;
    return true;
}

bool AmfNode::authenticate_n12(const std::string& imsi, const std::string& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool authenticated = control_plane_.authenticate_n12(operational, registration_.has_ue(imsi), imsi, request);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N12", authenticated, peers_.n12 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!authenticated) {
        return false;
    }
    ++stats_.n12_auth_requests;
    return true;
}

bool AmfNode::transfer_n14_context(const std::string& imsi, const std::string& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool transferred = interworking_.transfer_n14_context(operational, registration_.has_ue(imsi), imsi, request);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N14", transferred, peers_.n14 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!transferred) {
        return false;
    }
    ++stats_.n14_context_transfers;
    return true;
}

bool AmfNode::query_n15_policy(const std::string& imsi, const std::string& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool queried = control_plane_.query_n15_policy(operational, registration_.has_ue(imsi), imsi, request);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N15", queried, peers_.n15 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!queried) {
        return false;
    }
    ++stats_.n15_policy_queries;
    return true;
}

bool AmfNode::select_n22_slice(const std::string& imsi, const std::string& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool selected = control_plane_.select_n22_slice(operational, registration_.has_ue(imsi), imsi, request);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N22", selected, peers_.n22 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!selected) {
        return false;
    }
    ++stats_.n22_slice_selections;
    return true;
}

bool AmfNode::interwork_n26(const std::string& imsi, const std::string& operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto started = std::chrono::steady_clock::now();

    const bool operational = is_operational();
    const bool interworked = interworking_.interwork_n26(operational, registration_.has_ue(imsi), imsi, operation);
    const double latency_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    record_interface_activity("N26", interworked, peers_.n26 == nullptr ? "detached" : (operational ? "service-reject" : "admin-down"), latency_ms);
    if (!interworked) {
        return false;
    }
    ++stats_.n26_interworking_ops;
    return true;
}

bool AmfNode::set_plmn(const std::string& mcc, const std::string& mnc) {
    std::lock_guard<std::mutex> lock(mutex_);

    return control_plane_.set_plmn(mcc, mnc);
}

bool AmfNode::set_alarm_thresholds(const AlarmThresholds& thresholds) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (thresholds.warning_error_rate_percent < 0.0 || thresholds.warning_error_rate_percent > 100.0) {
        return false;
    }

    if (thresholds.critical_error_rate_percent < 0.0 || thresholds.critical_error_rate_percent > 100.0) {
        return false;
    }

    if (thresholds.warning_error_rate_percent > thresholds.critical_error_rate_percent) {
        return false;
    }

    if (thresholds.critical_error_count == 0) {
        return false;
    }

    alarm_thresholds_ = thresholds;
    return true;
}

std::vector<InterfaceInfo> AmfNode::list_interfaces() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return {
        {"N1", "control-plane", peers_.n1 != nullptr},
        {"N2", "control-plane", true},
        {"N3", "user-plane", peers_.n3 != nullptr},
        {"N8", "control-plane", peers_.n8 != nullptr},
        {"N11", "control-plane", peers_.n11 != nullptr},
        {"N12", "control-plane", peers_.n12 != nullptr},
        {"N14", "interworking", peers_.n14 != nullptr},
        {"N15", "control-plane", peers_.n15 != nullptr},
        {"N22", "control-plane", peers_.n22 != nullptr},
        {"N26", "interworking", peers_.n26 != nullptr},
        {"SBI", "control-plane", true},
    };
}

std::vector<InterfaceDiagnostics> AmfNode::list_interface_diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<InterfaceDiagnostics> out;
    out.reserve(interface_diagnostics_.size());

    const bool available = is_operational();
    const auto push_iface = [&](const std::string& name) {
        const auto it = interface_diagnostics_.find(name);
        if (it == interface_diagnostics_.end()) {
            return;
        }

        auto diag = it->second;
        diag.available = diag.configured && available;
        diag.status_reason = derive_status_reason(diag, available);
        diag.alarm_level = derive_alarm_level(diag, available);
        out.push_back(diag);
    };

    push_iface("N1");
    push_iface("N2");
    push_iface("N3");
    push_iface("N8");
    push_iface("N11");
    push_iface("N12");
    push_iface("N14");
    push_iface("N15");
    push_iface("N22");
    push_iface("N26");
    push_iface("SBI");

    return out;
}

std::vector<InterfaceErrorEvent> AmfNode::list_interface_errors_last(std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (limit == 0 || interface_error_history_.empty()) {
        return {};
    }

    const std::size_t count = std::min(limit, interface_error_history_.size());
    std::vector<InterfaceErrorEvent> out;
    out.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(interface_error_history_[interface_error_history_.size() - 1 - i]);
    }

    return out;
}

std::vector<InterfaceTelemetry> AmfNode::list_interface_telemetry(std::size_t window_seconds) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (window_seconds == 0) {
        window_seconds = 60;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto window = std::chrono::seconds(window_seconds);

    std::vector<InterfaceTelemetry> out;
    out.reserve(interface_diagnostics_.size());

    const auto push_iface = [&](const std::string& name) {
        const auto diag_it = interface_diagnostics_.find(name);
        if (diag_it == interface_diagnostics_.end()) {
            return;
        }

        InterfaceTelemetry t {};
        t.name = diag_it->second.name;
        t.plane = diag_it->second.plane;
        t.window_seconds = window_seconds;

        std::vector<double> latencies;
        const auto samples_it = interface_telemetry_.find(name);
        if (samples_it != interface_telemetry_.end()) {
            for (const auto& sample : samples_it->second) {
                if (now - sample.timestamp > window) {
                    continue;
                }

                ++t.attempts_in_window;
                if (sample.success) {
                    ++t.successes_in_window;
                }
                latencies.push_back(sample.latency_ms);
            }
        }

        if (t.attempts_in_window > 0) {
            t.success_rate_percent = (static_cast<double>(t.successes_in_window) * 100.0) / static_cast<double>(t.attempts_in_window);
        }

        if (!latencies.empty()) {
            std::sort(latencies.begin(), latencies.end());
            const auto idx50 = (latencies.size() - 1) * 50 / 100;
            const auto idx95 = (latencies.size() - 1) * 95 / 100;
            t.latency_p50_ms = latencies[idx50];
            t.latency_p95_ms = latencies[idx95];
        }

        out.push_back(t);
    };

    push_iface("N1");
    push_iface("N2");
    push_iface("N3");
    push_iface("N8");
    push_iface("N11");
    push_iface("N12");
    push_iface("N14");
    push_iface("N15");
    push_iface("N22");
    push_iface("N26");
    push_iface("SBI");

    return out;
}

AmfStatusSnapshot AmfNode::status() const {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::size_t active_ue = registration_.active_ue_count();

    return AmfStatusSnapshot {
        mobility_.state(),
        control_plane_.mcc(),
        control_plane_.mnc(),
        active_ue,
        stats_,
    };
}

void AmfNode::clear_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_ = {};
}

bool AmfNode::is_operational() const {
    return mobility_.is_operational();
}

void AmfNode::init_interface_diagnostics() {
    interface_diagnostics_.clear();
    interface_diagnostics_["N1"] = {"N1", "control-plane", peers_.n1 != nullptr, false, 0, 0, peers_.n1 != nullptr ? "admin-down" : "detached", peers_.n1 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N2"] = {"N2", "control-plane", true, false, 0, 0, "admin-down", "warning", "never"};
    interface_diagnostics_["N3"] = {"N3", "user-plane", peers_.n3 != nullptr, false, 0, 0, peers_.n3 != nullptr ? "admin-down" : "detached", peers_.n3 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N8"] = {"N8", "control-plane", peers_.n8 != nullptr, false, 0, 0, peers_.n8 != nullptr ? "admin-down" : "detached", peers_.n8 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N11"] = {"N11", "control-plane", peers_.n11 != nullptr, false, 0, 0, peers_.n11 != nullptr ? "admin-down" : "detached", peers_.n11 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N12"] = {"N12", "control-plane", peers_.n12 != nullptr, false, 0, 0, peers_.n12 != nullptr ? "admin-down" : "detached", peers_.n12 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N14"] = {"N14", "interworking", peers_.n14 != nullptr, false, 0, 0, peers_.n14 != nullptr ? "admin-down" : "detached", peers_.n14 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N15"] = {"N15", "control-plane", peers_.n15 != nullptr, false, 0, 0, peers_.n15 != nullptr ? "admin-down" : "detached", peers_.n15 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N22"] = {"N22", "control-plane", peers_.n22 != nullptr, false, 0, 0, peers_.n22 != nullptr ? "admin-down" : "detached", peers_.n22 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["N26"] = {"N26", "interworking", peers_.n26 != nullptr, false, 0, 0, peers_.n26 != nullptr ? "admin-down" : "detached", peers_.n26 != nullptr ? "warning" : "none", "never"};
    interface_diagnostics_["SBI"] = {"SBI", "control-plane", true, false, 0, 0, "admin-down", "warning", "never"};
}

void AmfNode::record_interface_activity(const std::string& iface_name, bool success, const std::string& failure_reason, double latency_ms) {
    auto it = interface_diagnostics_.find(iface_name);
    if (it == interface_diagnostics_.end()) {
        return;
    }

    auto& samples = interface_telemetry_[iface_name];
    samples.push_back(TelemetrySample {std::chrono::steady_clock::now(), latency_ms, success});
    while (samples.size() > kMaxTelemetrySamples) {
        samples.pop_front();
    }

    it->second.last_activity_utc = now_utc();
    if (success) {
        ++it->second.success_count;
        it->second.status_reason = "ok";
        return;
    }

    ++it->second.error_count;
    InterfaceErrorEvent event {};
    event.interface_name = iface_name;
    event.reason = failure_reason.empty() ? "service-reject" : failure_reason;
    event.timestamp_utc = it->second.last_activity_utc;
    interface_error_history_.push_back(event);
    if (interface_error_history_.size() > kMaxInterfaceErrorHistory) {
        interface_error_history_.pop_front();
    }

    if (!failure_reason.empty()) {
        it->second.status_reason = failure_reason;
    } else {
        it->second.status_reason = "service-reject";
    }
}

std::string AmfNode::derive_status_reason(const InterfaceDiagnostics& diag, bool operational) const {
    if (!diag.configured) {
        return "detached";
    }

    if (!operational) {
        return "admin-down";
    }

    if (diag.error_count > 0 && diag.success_count == 0) {
        return "service-reject";
    }

    if (diag.status_reason == "service-reject") {
        return "service-reject";
    }

    if (diag.success_count > 0) {
        return "ok";
    }

    return "ok";
}

std::string AmfNode::derive_alarm_level(const InterfaceDiagnostics& diag, bool operational) const {
    if (!diag.configured) {
        return "none";
    }

    if (!operational) {
        return alarm_thresholds_.admin_down_warning ? "warning" : "none";
    }

    const std::size_t attempts = diag.success_count + diag.error_count;
    if (attempts == 0 || diag.error_count == 0) {
        return "none";
    }

    const double error_rate = static_cast<double>(diag.error_count) * 100.0 / static_cast<double>(attempts);
    if (error_rate >= alarm_thresholds_.critical_error_rate_percent || diag.error_count >= alarm_thresholds_.critical_error_count) {
        return "critical";
    }

    if (error_rate >= alarm_thresholds_.warning_error_rate_percent) {
        return "warning";
    }

    return "none";
}

}  // namespace amf
