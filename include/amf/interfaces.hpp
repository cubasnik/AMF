#pragma once

#include <optional>
#include <string>
#include <vector>

namespace amf {

enum class AmfState {
    Idle,
    Initializing,
    Running,
    Degraded,
    Stopped,
};

enum class UeState {
    Registered,
    Deregistered,
};

struct UeContext {
    std::string imsi;
    std::string tai;
    UeState state {UeState::Registered};
    std::string last_seen_utc;
};

struct AmfStats {
    std::size_t starts {0};
    std::size_t stops {0};
    std::size_t heartbeat_ticks {0};
    std::size_t ue_registrations {0};
    std::size_t ue_deregistrations {0};
    std::size_t n2_signaling_messages {0};
    std::size_t sbi_notifications {0};
    std::size_t n1_messages {0};
    std::size_t n3_user_plane_packets {0};
    std::size_t n8_subscription_queries {0};
    std::size_t n11_pdu_session_ops {0};
    std::size_t n12_auth_requests {0};
    std::size_t n14_context_transfers {0};
    std::size_t n15_policy_queries {0};
    std::size_t n22_slice_selections {0};
    std::size_t n26_interworking_ops {0};
};

struct AmfStatusSnapshot {
    AmfState state {AmfState::Idle};
    std::string mcc {"250"};
    std::string mnc {"03"};
    std::size_t active_ue {0};
    AmfStats stats {};
};

struct InterfaceInfo {
    std::string name;
    std::string plane;
    bool configured {false};
};

struct AlarmThresholds {
    double warning_error_rate_percent {10.0};
    double critical_error_rate_percent {50.0};
    std::size_t critical_error_count {3};
    bool admin_down_warning {true};
};

struct InterfaceDiagnostics {
    std::string name;
    std::string plane;
    bool configured {false};
    bool available {false};
    std::size_t success_count {0};
    std::size_t error_count {0};
    std::string status_reason {"unknown"};
    std::string alarm_level {"none"};
    std::string last_activity_utc {"never"};
    std::size_t sbi_timeout_failures {0};
    std::size_t sbi_connect_failures {0};
    std::size_t sbi_non_2xx_failures {0};
    std::size_t sbi_circuit_open_rejections {0};
    bool sbi_circuit_open {false};
};

struct InterfaceErrorEvent {
    std::string interface_name;
    std::string reason;
    std::string timestamp_utc;
};

struct InterfaceTelemetry {
    std::string name;
    std::string plane;
    std::size_t window_seconds {60};
    std::size_t attempts_in_window {0};
    std::size_t successes_in_window {0};
    double success_rate_percent {0.0};
    double latency_p50_ms {0.0};
    double latency_p95_ms {0.0};
};

struct SbiFailureCounters {
    std::size_t timeout_failures {0};
    std::size_t connect_failures {0};
    std::size_t non_2xx_failures {0};
    std::size_t circuit_open_rejections {0};
    bool circuit_open {false};
};

class IN2Interface {
public:
    virtual ~IN2Interface() = default;
    virtual void deliver_nas(const std::string& imsi, const std::string& payload) = 0;
};

class IN1Interface {
public:
    virtual ~IN1Interface() = default;
    virtual void send_nas_to_ue(const std::string& imsi, const std::string& payload) = 0;
};

class IN3Interface {
public:
    virtual ~IN3Interface() = default;
    virtual void forward_user_plane(const std::string& imsi, const std::string& payload) = 0;
};

class IN8Interface {
public:
    virtual ~IN8Interface() = default;
    virtual void query_subscription(const std::string& imsi, const std::string& request) = 0;
};

class IN11Interface {
public:
    virtual ~IN11Interface() = default;
    virtual void manage_pdu_session(const std::string& imsi, const std::string& operation) = 0;
};

class IN12Interface {
public:
    virtual ~IN12Interface() = default;
    virtual void authenticate_ue(const std::string& imsi, const std::string& request) = 0;
};

class IN14Interface {
public:
    virtual ~IN14Interface() = default;
    virtual void transfer_amf_context(const std::string& imsi, const std::string& request) = 0;
};

class IN15Interface {
public:
    virtual ~IN15Interface() = default;
    virtual void query_policy(const std::string& imsi, const std::string& request) = 0;
};

class IN22Interface {
public:
    virtual ~IN22Interface() = default;
    virtual void select_network_slice(const std::string& imsi, const std::string& request) = 0;
};

class IN26Interface {
public:
    virtual ~IN26Interface() = default;
    virtual void interwork_with_mme(const std::string& imsi, const std::string& operation) = 0;
};

struct AmfPeerInterfaces {
    IN1Interface* n1 {nullptr};
    IN3Interface* n3 {nullptr};
    IN8Interface* n8 {nullptr};
    IN11Interface* n11 {nullptr};
    IN12Interface* n12 {nullptr};
    IN14Interface* n14 {nullptr};
    IN15Interface* n15 {nullptr};
    IN22Interface* n22 {nullptr};
    IN26Interface* n26 {nullptr};
};

class ISbiInterface {
public:
    virtual ~ISbiInterface() = default;
    virtual bool notify_service(const std::string& service_name, const std::string& payload) = 0;
    virtual std::string last_failure_reason() const {
        return "service-reject";
    }
    virtual std::optional<SbiFailureCounters> failure_counters() const {
        return std::nullopt;
    }
};

class IAmfLifecycle {
public:
    virtual ~IAmfLifecycle() = default;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool set_degraded() = 0;
    virtual bool recover() = 0;
    virtual void tick() = 0;
};

class IAmfUeManager {
public:
    virtual ~IAmfUeManager() = default;
    virtual bool register_ue(const std::string& imsi, const std::string& tai) = 0;
    virtual bool deregister_ue(const std::string& imsi) = 0;
    virtual std::optional<UeContext> find_ue(const std::string& imsi) const = 0;
    virtual std::vector<UeContext> list_ue() const = 0;
};

class IAmfNode : public IAmfLifecycle, public IAmfUeManager {
public:
    ~IAmfNode() override = default;

    virtual bool send_n2_nas(const std::string& imsi, const std::string& payload) = 0;
    virtual bool notify_sbi(const std::string& service_name, const std::string& payload) = 0;
    virtual bool send_n1_nas(const std::string& imsi, const std::string& payload) = 0;
    virtual bool forward_n3_user_plane(const std::string& imsi, const std::string& payload) = 0;
    virtual bool query_n8_subscription(const std::string& imsi, const std::string& request = "get-am-data") = 0;
    virtual bool manage_n11_pdu_session(const std::string& imsi, const std::string& operation) = 0;
    virtual bool authenticate_n12(const std::string& imsi, const std::string& request = "auth-request") = 0;
    virtual bool transfer_n14_context(const std::string& imsi, const std::string& request = "context-transfer") = 0;
    virtual bool query_n15_policy(const std::string& imsi, const std::string& request = "get-am-policy") = 0;
    virtual bool select_n22_slice(const std::string& imsi, const std::string& request = "select-slice") = 0;
    virtual bool interwork_n26(const std::string& imsi, const std::string& operation) = 0;
    virtual bool set_plmn(const std::string& mcc, const std::string& mnc) = 0;
    virtual bool set_alarm_thresholds(const AlarmThresholds& thresholds) = 0;
    virtual std::vector<InterfaceInfo> list_interfaces() const = 0;
    virtual std::vector<InterfaceDiagnostics> list_interface_diagnostics() const = 0;
    virtual std::vector<InterfaceErrorEvent> list_interface_errors_last(std::size_t limit) const = 0;
    virtual std::vector<InterfaceTelemetry> list_interface_telemetry(std::size_t window_seconds = 60) const = 0;

    virtual AmfStatusSnapshot status() const = 0;
    virtual void clear_stats() = 0;
};

const char* to_string(AmfState state);
const char* to_string(UeState state);

}  // namespace amf
